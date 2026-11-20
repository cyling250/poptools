#include "GoodyModule.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>

#include "../AppServices.h"
#include "core/assistant/ChatSession.h"
#include "core/assistant/DailyRecordJob.h"
#include "core/assistant/McpStdioClient.h"
#include "core/assistant/MemoryStore.h"
#include "core/assistant/MonthlyRecordJob.h"
#include "core/assistant/Scheduler.h"
#include "core/assistant/WorkingMemory.h"
#include "core/monitor/ActivityStore.h"
#include "core/assistant/FsTool.h"
#include "core/assistant/LlmClient.h"
#include "core/assistant/MonitorQueryTool.h"
#include "core/assistant/PermissionManager.h"
#include "core/assistant/ShellTool.h"
#include "core/assistant/ToolRegistry.h"
#include "core/assistant/ToolText.h"
#include "core/hotkey/GlobalHotKey.h"
#include "core/hotkey/HotKeyNames.h"
#include "core/settings/Settings.h"
#include "ui/goody/GoodyBallLogic.h"
#include "ui/goody/GoodyFloatingBall.h"
#include "ui/goody/GoodyWindow.h"

namespace poptools {

namespace {
constexpr int kMaxToolRounds = 8; // 单次用户消息最多 8 轮工具往返（防失控）

// 数据目录拼接去重（T-062）：goody/ 与 monitor/ 子目录多处使用，集中一处
// 保证路径口径唯一（QStandardPaths::AppLocalDataLocation 进程内恒定）
QString goodyDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/goody");
}

QString monitorDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/monitor");
}
} // namespace

// 仅作 QObject 接线中枢；子对象经父子关系随模块析构（m_* 多为 this 子对象）
GoodyModule::GoodyModule(QObject *parent)
    : QObject(parent)
{
}

// 装配顺序敏感：auditStore → perm/client/session/memory → 注册工具/MCP →
// 日记录任务/调度 → 热键/托盘 → 悬浮球 → 数据流接线
void GoodyModule::init(AppServices &services)
{
    m_services = &services;
    m_registry = services.toolRegistry;
    Q_ASSERT(m_registry && "组合根必须先创建共享 ToolRegistry 再装配模块");

    // 审计存储（goody 目录；与 monitor 目录的 ActivityStore 各自独立线程）
    m_auditStore = new ActivityStore(goodyDir(), this);
    m_auditStore->start();

    m_perm = new PermissionManager(m_auditStore, this);
    m_perm->setToolRegistry(m_registry); // readOnly 判定以注册表为准（app.* 不再一刀切放行）
    // T-062 修复: init 原先不应用持久化权限设置（PermissionManager 构造不读
    // Settings，默认 Ask+空白名单）——重启后用户配置的 Smart/自动模式、"总是
    // 允许"清单、shell 白名单全部失效回到逐条确认。此处与 onSettingsChanged
    // 共用同一重应用路径（键名/格式不变，只补装配时机）
    applyPermissionSettings();
    m_client = new LlmClient(this);
    m_session = new ChatSession(goodyDir() + QStringLiteral("/chat/sessions.json"), this);
    m_session->load();
    m_memory = new MemoryStore(goodyDir(), this);
    m_memory->load();
    m_work = new WorkingMemory(goodyDir(), this);

    registerBuiltinTools();
    setupMcp();

    // ---- 本日工作记录 + 月度摘要（T-067）：启动补账 + 每日到点 + 静默
    // 起居注刷新 + 开机滚动月报 + 工具手动触发 ----
    m_recordJob = new DailyRecordJob(monitorDir(), goodyDir(), this);
    m_monthlyJob = new MonthlyRecordJob(goodyDir(), this);
    m_scheduler = new Scheduler(this);
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(20 * 60 * 1000); // 起居注心跳：20min 一判
    connect(m_refreshTimer, &QTimer::timeout, this, [this] {
        if (auto *p = MainThreadProbe::instance())
            p->mark("goody_heartbeat"); // T-070b：后台停摆归因打点
        m_recordJob->maybeRefreshToday([this](bool changed) {
            if (changed && m_services)
                emit m_services->goodySummariesChanged(); // 静默：只发数据变更
        });
    });
    // T-067：工作摘要查询槽（桌面日历消费）——路径即存在性，内容由消费方读
    services.goodyDailyDigestPath = [this](int y, int m, int d) {
        const QDate date(y, m, d);
        if (!date.isValid())
            return QString();
        const QString p = goodyDir() + QStringLiteral("/daily/")
                          + date.toString(QStringLiteral("yyyy-MM-dd"))
                          + QStringLiteral(".md");
        return QFile::exists(p) ? p : QString();
    };
    services.goodyMonthlyDigestPath = [this](int y, int m) {
        const QString p = MonthlyRecordJob::monthPath(goodyDir(), y, m);
        return QFile::exists(p) ? p : QString();
    };
    applySummarySchedule();
    // 启动（延迟 5s：等事件循环与网络就绪）：历史日补账 + 当月滚动月报
    //（【月报更新】；无日摘要不烧 LLM；往月冻结不自动重算）
    if (Settings::instance().assistantAutoSummarize()) {
        QTimer::singleShot(5000, this, [this] {
            if (!Settings::instance().assistantAutoSummarize())
                return;
            m_recordJob->processPending(false, [](int, const QString &) {});
            const QDate today = QDate::currentDate();
            if (MonthlyRecordJob::monthHasDailyData(goodyDir(), today.year(),
                                                    today.month()))
                m_monthlyJob->runForMonth(today.year(), today.month(),
                                          [this](bool ok, const QString &) {
                                              if (ok && m_services)
                                                  emit m_services->goodySummariesChanged();
                                          });
        });
    }

    // 呼出热键（注册失败由组合根统一气泡告知；R5-D2：注册返回值用于
    // 托盘文案标注，不再静默）
    const quint32 hkVk = Settings::instance().assistantHotKeyVk();
    const quint32 hkMods = Settings::instance().assistantHotKeyModifiers();
    const bool hkOk = services.registerHotKey(hkVk, hkMods, [this] {
        // T-062 修复: 热键回调在 GlobalHotKey 的原生事件过滤器栈中直发，
        // 直接建窗/激活违反 AppServices 契约（"GUI 操作请自行延迟到事件
        // 循环"）——对齐 PinModule/划词翻译的 singleShot 先例
        QTimer::singleShot(0, QCoreApplication::instance(), [this] { openChat(); });
    });

    // R5-D2：托盘文案的快捷键改为按设置动态生成（ini 改 assistantHotKeyVk
    // 后不再与事实不符）；注册失败时追加标注（CaptureModule 的"已被占用"
    // 托盘文案先例）
    // R5-D2 复审修正（P3-4）：失败标注对齐 CaptureModule 的括注先例风格，
    // 原因取 GlobalHotKey::lastError 的具体原因——常见即"已被其他程序占用"，
    // 进程内冲突/Win32 错误码场景也比笼统"注册失败"可排查
    // T-069b 托盘菜单精简：「Goody 管家」托盘入口撤除，打开动作改由设置页
    //「打开 Goody 管家」卡片消费（热键与悬浮球入口不变）
    services.goodyOpenChat = [this] { openChat(); };

    // T-050：桌面悬浮球（开关默认开；关=销毁，设置页即时重应用）
    applyBallSettings();

    // 聊天数据流
    connect(m_client, &LlmClient::contentDelta, this, [this](const QString &delta) {
        if (m_window)
            m_window->appendAssistantDelta(delta);
    });
    // T-050-P2：思考增量 → 思考折叠区（流式，默认折叠，标题行报字数）
    connect(m_client, &LlmClient::reasoningDelta, this, [this](const QString &delta) {
        if (m_window)
            m_window->appendReasoningDelta(delta);
    });
    connect(m_client, &LlmClient::finished, this, &GoodyModule::onTurnFinished);

    connect(&services, &AppServices::settingsChanged, this, &GoodyModule::onSettingsChanged);
}

// 退出前落盘：会话全量保存 + 审计流冲刷停止；MCP 子进程随 m_mcpClients
//（QObject 子对象）析构终止
void GoodyModule::shutdown()
{
    if (m_services)
        m_services->goodyOpenChat = nullptr; // T-069b：能力槽撤销防悬垂
    if (m_session)
        m_session->save();
    if (m_auditStore)
        m_auditStore->stop();
}

// 设置页保存后的三路重应用：权限参数 / 总结调度 / 悬浮球开关（各自内部幂等）
void GoodyModule::onSettingsChanged()
{
    // 权限配置即时重读（模式/白名单在设置页修改后生效）
    applyPermissionSettings();
    // 总结调度接线：总结时刻/自动开关改动即时生效（原实现须重启）
    applySummarySchedule();
    // T-050：悬浮球开关/位置改动即时重应用（开=建+显示，关=销毁）
    applyBallSettings();
}

// 权限三参数重应用（init 装配与设置变更共用）：模式/"总是允许"清单/shell
// 白名单一次性从 Settings 读入。各 setter 均为幂等赋值，重复调用无副作用
void GoodyModule::applyPermissionSettings()
{
    if (!m_perm)
        return;
    const Settings &s = Settings::instance();
    m_perm->setMode(PermissionManager::modeFromSetting(s.assistantPermMode()));
    m_perm->setAlwaysAllowTools(s.assistantAlwaysAllowTools());
    m_perm->setShellAllowPrefixes(s.assistantShellAllowPrefixes());
}

// 总结调度接线（init 与设置变更共用）：按当前设置注册/启停每日任务与
// 起居注心跳（T-067）
void GoodyModule::applySummarySchedule()
{
    if (!m_scheduler || !m_recordJob)
        return;
    const Settings &s = Settings::instance();
    if (!s.assistantAutoSummarize()) {
        m_scheduler->stop();
        if (m_refreshTimer)
            m_refreshTimer->stop();
        return;
    }
    m_scheduler->setDailyTask(
        QStringLiteral("dailySummary"),
        QTime::fromString(s.assistantSummaryTime(), QStringLiteral("HH:mm")),
        [this] { m_recordJob->processPending(true, [](int n, const QString &err) {
                     // 起居注纪律（T-067）：静默成文，不弹气泡不通知
                     Q_UNUSED(n);
                     Q_UNUSED(err);
                 }); });
    m_scheduler->start();
    if (m_refreshTimer)
        m_refreshTimer->start();
}

// 内置工具注册：time.now 有 has() 防重（其余 registerTool 覆盖语义天然幂等）；
// fs/shell/memory/monitor/generate_daily_record 各自锚定异步交付对象
void GoodyModule::registerBuiltinTools()
{
    // time.now 的 has() 是给单测共用注册表兜底；内置工具重复注册走覆盖
    // 语义，天然幂等
    if (!m_registry->has(QStringLiteral("time.now"))) {
        ToolDef d;
        d.name = QStringLiteral("time.now");
        d.readOnly = true;
        d.description = QStringLiteral("获取当前日期时间（本地时区）。");
        d.parameters = emptyObjectSchema();
        d.fn = [](const QJsonObject &, const std::function<void(QJsonObject)> &done) {
            const QDateTime now = QDateTime::currentDateTime();
            done(QJsonObject{{"ok", true},
                             {"datetime", now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss ddd"))},
                             {"epochMs", double(now.toMSecsSinceEpoch())}});
        };
        m_registry->registerTool(d);
    }

    for (const ToolDef &d : makeFsTools(this)) // R6-C1: this 作异步交付锚点
        m_registry->registerTool(d);
    m_registry->registerTool(makeShellRunTool());
    if (m_memory && m_work) {
        for (const ToolDef &d : makeMemoryTools(m_memory, m_work))
            m_registry->registerTool(d);
    }
    if (m_services) {
        m_registry->registerTool(makeMonitorQueryTool(monitorDir(), this));

        // 手动生成某天的本日工作记录（用户："帮我把昨天的记录生成一下"）
        ToolDef gen;
        gen.name = QStringLiteral("app.generate_daily_record");
        gen.readOnly = true; // 只读日志 + 写 goody 自己的 daily 目录
        gen.description = QStringLiteral(
            "为指定日期生成本日工作记录（基于活动监控日志 + 大模型归纳，"
            "写入 goody/daily/<日期>.md）。date 省略时生成昨天。");
        gen.parameters = QJsonObject{
            {"type", QStringLiteral("object")},
            {"properties",
             QJsonObject{{"date", QJsonObject{{"type", QStringLiteral("string")},
                                              {"description", QStringLiteral("YYYY-MM-DD，默认昨天")}}}}}};
        gen.fn = [this](const QJsonObject &args, const std::function<void(QJsonObject)> &done) {
            const QString date = args.value(QStringLiteral("date")).toString().isEmpty()
                ? QDate::currentDate().addDays(-1).toString(QStringLiteral("yyyy-MM-dd"))
                : args.value(QStringLiteral("date")).toString();
            m_recordJob->runForDate(date, [done, date](bool ok, const QString &error) {
                if (ok)
                    done(QJsonObject{{"ok", true},
                                     {"note", QStringLiteral("已生成 %1 的工作记录（goody/daily）")
                                                   .arg(date)}});
                else
                    done(QJsonObject{{"ok", false}, {"error", error}});
            });
        };
        m_registry->registerTool(gen);

        // 重算指定月份的滚动月报（用户："把 10 月的月报重新生成一下"）。
        // 当月开机自动重算；往月冻结，仅经本工具（对话）触发——无任何按钮
        ToolDef genM;
        genM.name = QStringLiteral("app.generate_monthly_record");
        genM.readOnly = true; // 只读日摘要 + 写 goody 自己的 monthly 目录
        genM.description = QStringLiteral(
            "生成或重算指定月份的工作月报（汇总该月每日工作记录 + 大模型归纳，"
            "写入 goody/monthly/<年-月>.md）。year/month 省略时取当前月。");
        genM.parameters = QJsonObject{
            {"type", QStringLiteral("object")},
            {"properties",
             QJsonObject{{"year", QJsonObject{{"type", QStringLiteral("integer")},
                                              {"description", QStringLiteral("四位年份，默认当前年")}}},
                         {"month", QJsonObject{{"type", QStringLiteral("integer")},
                                               {"description", QStringLiteral("1-12，默认当前月")}}}}}};
        genM.fn = [this](const QJsonObject &args,
                         const std::function<void(QJsonObject)> &done) {
            const QDate today = QDate::currentDate();
            const int year = args.value(QStringLiteral("year")).toInt(today.year());
            const int month = args.value(QStringLiteral("month")).toInt(today.month());
            m_monthlyJob->runForMonth(year, month,
                                      [this, year, month, done](bool ok, const QString &error) {
                                          if (ok) {
                                              if (m_services)
                                                  emit m_services->goodySummariesChanged();
                                              done(QJsonObject{
                                                  {"ok", true},
                                                  {"note", QStringLiteral("已生成 %1 月报（goody/monthly）")
                                                               .arg(MonthlyRecordJob::monthKey(
                                                                   year, month))}});
                                          } else {
                                              done(QJsonObject{{"ok", false}, {"error", error}});
                                          }
                                      });
        };
        m_registry->registerTool(genM);
    }
}

// mcp.json 服务器配置装载：建连接对象但不启动（懒启动，见
// ensureMcpStarted）；就绪后 listTools → 注册 mcp__<server>__<tool>。
// MCP 是可选能力：无配置文件即零开销
void GoodyModule::setupMcp()
{
    const QList<McpStdioClient::ServerConfig> servers = McpStdioClient::loadConfig(goodyDir());
    for (const McpStdioClient::ServerConfig &cfg : servers) {
        auto *client = new McpStdioClient(cfg, this);
        m_mcpClients << client;
        connect(client, &McpStdioClient::readyChanged, this, [this, client] {
            if (!client->ready())
                return;
            client->listTools([this, client](bool ok, const QJsonArray &tools, const QString &err) {
                if (!ok) {
                    qWarning("Goody: MCP[%s] tools/list 失败：%s",
                             qPrintable(client->serverName()), qPrintable(err));
                    return;
                }
                for (const QJsonValue &v : tools) {
                    const QJsonObject t = v.toObject();
                    const QString toolName = t.value(QStringLiteral("name")).toString();
                    if (toolName.isEmpty())
                        continue;
                    ToolDef d;
                    d.name = QStringLiteral("mcp__%1__%2").arg(client->serverName(), toolName);
                    d.readOnly = false; // 外部能力未知风险 → 一律走权限确认
                    d.description = QStringLiteral("[MCP:%1] %2")
                                        .arg(client->serverName(),
                                             t.value(QStringLiteral("description"))
                                                 .toString());
                    d.parameters = t.value(QStringLiteral("inputSchema")).toObject();
                    if (d.parameters.isEmpty())
                        d.parameters = emptyObjectSchema();
                    const QPointer<McpStdioClient> anchor(client);
                    d.fn = [this, anchor, toolName](
                               const QJsonObject &args,
                               const std::function<void(QJsonObject)> &done) {
                        if (!anchor) {
                            done(QJsonObject{{"ok", false},
                                             {"error", QStringLiteral("MCP 服务已断开")}});
                            return;
                        }
                        anchor->callTool(
                            toolName, args,
                            [done](bool callOk, const QJsonArray &content, const QString &err) {
                                // content 数组 → 文本聚合（MCP text 块）
                                QString text;
                                for (const QJsonValue &cv : content) {
                                    const QJsonObject co = cv.toObject();
                                    if (co.value(QStringLiteral("type")).toString()
                                        == QLatin1String("text"))
                                        text += co.value(QStringLiteral("text")).toString();
                                }
                                if (!err.isEmpty())
                                    done(QJsonObject{{"ok", false},
                                                     {"error", err},
                                                     {"text", text}});
                                else
                                    done(QJsonObject{{"ok", callOk}, {"text", text}});
                            });
                    };
                    m_registry->registerTool(d);
                }
            });
        });
        // R6-C2: 不在此 start——子进程常驻内存大（单 server 数十~数百 MB），
        // 应用启动期并无对话需求，改由 ensureMcpStarted 懒启动
    }
}

// R6-C2: MCP 子进程懒启动。触发点核对结论：mcp__* 工具在握手完成后才注册
// 进 ToolRegistry（readyChanged → listTools → registerTool），未启动时 LLM
// 侧可见工具清单本就不含它们——"首次调用 mcp__* 工具"在未启动状态下不可达，
// 故唯一可达触发点是打开聊天窗（openChat）。无 mcp.json 时 m_mcpClients
// 为空，本函数零开销（维持现状）
void GoodyModule::ensureMcpStarted()
{
    // T-062 修复: 原先 m_mcpStarted 一次性置位——McpStdioClient 握手超时
    // stop 后永无重试机会（MCP 永久哑火）。start() 自身幂等（进程在手即
    // 返回），此处每次 openChat 都过一遍：运行中/已就绪的 client 零开销，
    // 仅握手失败被 stop 的 client 获得重试
    for (McpStdioClient *client : m_mcpClients)
        client->start();
}

// 懒建聊天窗并接全部窗口→模块请求（发送/停止/清空/侧栏四件套/重新生成/
// 权限回包）；所有域操作先过 turnLocked 守卫，会话清空走 epoch 收束
void GoodyModule::ensureWindow()
{
    if (m_window)
        return;
    // 无父窗口：进程退出由 OS 回收（析构不执行）——几何在 hideEvent 即时
    // 落盘、会话由模块 shutdown 保存，均无析构依赖
    m_window = new GoodyWindow();
    m_window->setSession(m_session); // T-050-P2：会话域对象注入（侧栏/历史/四件套）
    connect(m_window, &GoodyWindow::sendRequested, this, &GoodyModule::sendMessage);
    connect(m_window, &GoodyWindow::stopRequested, this, [this] { m_client->abort(); });
    connect(m_window, &GoodyWindow::sessionCleared, this, [this] {
        // 会话清空收束（B-2/C-N2）：一律推进代数——工具期在途回调（确认/
        // 执行）与 busy 期在途 LLM 回包都以代数失配丢弃，任何半截内容都
        // 不得写进刚清空的会话（断裂序列会让之后每轮请求被 API 整包拒绝）
        ++m_toolEpoch;
        if (m_toolPhase) {
            // 工具执行期清空：放弃剩余工具（按拒绝语义补审计），在途回调经
            // epoch 失配丢弃；窗口侧工具期已禁用清空按钮，此处是兜底
            for (const QJsonValue &tc : m_pendingToolCalls) {
                const QJsonObject fn =
                    tc.toObject().value(QStringLiteral("function")).toObject();
                m_perm->audit(m_registry->fromApiName(
                                  fn.value(QStringLiteral("name")).toString()),
                              QJsonDocument::fromJson(fn.value(QStringLiteral("arguments"))
                                                          .toString()
                                                          .toUtf8())
                                  .object(),
                              QStringLiteral("denied"), false);
            }
            m_pendingToolCalls = QJsonArray{};
            m_toolPhase = false;
            if (m_window) {
                // T-062 修复: 兜底路径原先漏复位窗口侧工具期（模块侧 m_toolPhase
                // 已复位但按钮禁用残留）——与 onTurnFinished/toolCallDone 各
                // 收束点成对调用保持一致
                m_window->setToolPhase(false);
                m_window->showErrorLine(QStringLiteral("工具执行已随会话清空中止"));
            }
        }
        // busy 期清空（C-N2）：abort 在途 LLM 请求——其 finished 回包带旧代数
        // 到达，onTurnFinished 丢弃（不 append、不走工具链）。窗口侧 busy 已
        // 禁用清空按钮，这是绕过 UI 的兜底路径
        if (m_client->busy())
            m_client->abort();
        m_session->clear();
        m_session->save();
        updateWorkingState(); // T-050：清空收束（abort+工具期放弃）后同步工作态
    });

    // ---- T-050-P2：会话侧栏与消息四件套（域操作统一在模块侧守卫执行，
    //      busy/工具期一律拒绝——中途切会话等价 C-N2 迟到回包场景） ----
    connect(m_window, &GoodyWindow::sessionSwitchRequested, this, [this](const QString &id) {
        if (turnLocked())
            return;
        m_session->switchSession(id); // changed 信号驱动窗口重建气泡列表
        m_session->saveSoon();
    });
    connect(m_window, &GoodyWindow::sessionCreateRequested, this, [this] {
        if (turnLocked())
            return;
        m_session->createSession();
        m_session->saveSoon();
    });
    connect(m_window, &GoodyWindow::sessionDeleteRequested, this, [this](const QString &id) {
        if (turnLocked())
            return;
        m_session->deleteSession(id); // 删当前 → 域内自动切最近会话或空会话
        m_session->saveSoon();
    });
    // 编辑重发（AI 代决语义）：从该消息截断其后全部（含自身），输入框
    // 预填原文本，用户可改后重发
    connect(m_window, &GoodyWindow::editResendRequested, this, [this](int sessionIndex) {
        if (turnLocked())
            return;
        const QJsonArray msgs = m_session->messages();
        if (sessionIndex < 0 || sessionIndex >= msgs.size())
            return;
        const QString text =
            msgs.at(sessionIndex).toObject().value(QStringLiteral("content")).toString();
        m_session->truncateFrom(sessionIndex, true);
        m_session->saveSoon();
        if (m_window) {
            m_window->reloadFromSession();
            m_window->setDraftInput(text);
        }
    });
    // 删除单条消息（assistant 连带其 reasoning——reasoning 内嵌随消息删除；
    // 域 API 自带 tool 配对组整删）
    connect(m_window, &GoodyWindow::messageDeleteRequested, this, [this](int sessionIndex) {
        if (turnLocked())
            return;
        if (!m_session->removeMessageAt(sessionIndex))
            return;
        m_session->saveSoon();
        if (m_window)
            m_window->reloadFromSession();
    });
    // 重新生成（AI 代决语义）：丢弃最后一条 user 之后全部（完整工具链一并
    // 回退，防孤儿序列）+ 以最后一条 user 重发（主流产品"从该消息分支重问"）
    connect(m_window, &GoodyWindow::regenerateRequested, this, [this] {
        if (turnLocked())
            return;
        const QJsonArray msgs = m_session->messages();
        int lastUser = -1;
        for (int i = msgs.size() - 1; i >= 0; --i) {
            if (msgs.at(i).toObject().value(QStringLiteral("role")).toString()
                == QLatin1String("user")) {
                lastUser = i;
                break;
            }
        }
        if (lastUser < 0) {
            if (m_window)
                m_window->showErrorLine(QStringLiteral("没有可重新生成的对话内容"));
            return;
        }
        m_session->truncateFrom(lastUser, false); // exclusive：保留该条 user，其后全弃
        m_session->saveSoon();
        // T-050-FIX2：重新生成是同一用户消息的新回合——工具轮次计数必须
        // 复位（sendMessage :573 是唯一复位点，本路径原先漏复位），否则
        // 新回合从 N 起步，反复重生成会提前触发 8 轮上限并把半截
        // tool_calls 塞进会话。T-070：新回合开新 turn 事件（dsh 轮次边界）
        m_toolRounds = 0;
        m_session->turnStart();
        if (m_window)
            m_window->reloadFromSession();
        runTurn(); // 既有回合循环（epoch 快照/呼吸灯/流式气泡照旧）
    });

    // 关窗：几何由窗口自管（assistant/windowGeometry，hideEvent 落盘）；
    // T-050：聊天窗隐没时悬浮球回归（隐没/回归以开关设置为准）
    connect(m_window, &GoodyWindow::windowHidden, this, [this] {
        if (m_ball && Settings::instance().assistantBallEnabled())
            m_ball->show();
    });
    connect(m_window, &QObject::destroyed, this, [this] { m_window = nullptr; });
}

// ---- T-050：桌面悬浮球 ----

// 懒建（幂等）：接线单击开窗、拖动位置持久化、工作呼吸光效与隐没同步
void GoodyModule::ensureBall()
{
    if (m_ball)
        return;
    // 无父窗口：进程退出由 OS 回收（析构不执行）——球无关键落盘（位置在
    // 拖动时即时持久化），无析构依赖
    m_ball = new GoodyFloatingBall();
    m_ball->applySavedPosition();
    connect(m_ball, &GoodyFloatingBall::clicked, this, [this] { openChat(); });
    connect(m_ball, &GoodyFloatingBall::positionChanged, this,
            [](const QPoint &pos) {
                Settings::instance().setAssistantBallPos(
                    goodyball::ballPosToString(pos));
            });
    connect(this, &GoodyModule::workingChanged, m_ball, &GoodyFloatingBall::setWorking);
    // 球建时若已在工作态（设置页销毁重建场景），立即同步呼吸光效
    m_ball->setWorking(m_working);
    connect(m_ball, &QObject::destroyed, this, [this] { m_ball = nullptr; });
}

// 开关重应用（init 与 settingsChanged 共用）：开=建+显示（聊天窗可见时
// 保持隐没）；关=销毁球。位置在球内 applySavedPosition 重钳制（换屏/改 ini 生效）
void GoodyModule::applyBallSettings()
{
    if (Settings::instance().assistantBallEnabled()) {
        ensureBall();
        m_ball->applySavedPosition();
        if (!m_window || !m_window->isVisible())
            m_ball->show();
        else
            m_ball->hide(); // 聊天窗开着：保持隐没
    } else if (m_ball) {
        m_ball->deleteLater(); // 关闭=销毁
        m_ball = nullptr;
    }
}

// 工作态去重广播：busy（LLM 请求在途）|| 工具执行期（tool_calls 回填/
// 权限确认等待）。调用点铺在回合状态机的各收束处，信号只在沿上变化
void GoodyModule::updateWorkingState()
{
    const bool working = (m_client && m_client->busy()) || m_toolPhase;
    if (working == m_working)
        return;
    m_working = working;
    emit workingChanged(working);
}

// 唯一自动唤起点：懒启动 MCP → 懒建窗口 → 前置/激活；悬浮球按开关隐没，
// 关窗经 windowHidden 回归
void GoodyModule::openChat()
{
    ensureMcpStarted(); // R6-C2: 首次打开聊天窗即拉起 MCP 子进程（幂等）
    ensureWindow();
    if (m_window->isVisible()) {
        m_window->raise();
        m_window->activateWindow();
        return;
    }
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
    // T-050：聊天时悬浮球隐没（球是唯一自动唤起点；关窗时经 windowHidden 回归）
    if (m_ball)
        m_ball->hide();
}

QString GoodyModule::buildSystemPrompt() const
{
    // 人格提示词（专业 / 温暖 / 只管电脑与记录的小管家）+ 动态上下文。
    // 边界写在最前（拒绝复杂任务优先级最高），工具纪律其次。
    QString prompt = QStringLiteral(
        "你是 Goody，PopTools 内置的电脑小管家。\n"
        "性格：专业、温暖、简洁。始终用中文回复，默认简短（不超过 10 行），"
        "列举步骤时用列表。称呼用户时自然礼貌，不谄媚。\n\n"
        "你的职责（严格边界）：\n"
        "1. 整理电脑：文件整理建议、磁盘清理、安装软件信息查询；\n"
        "2. 处理电脑相关问题：系统设置、常见故障排查、命令解释与执行；\n"
        "3. 做记录：工作日志、待办、备忘，以及基于本机活动日志生成「本日工作记录」。\n"
        "你不做：长文写作、大型编码项目、论文/法律/医疗等专业任务、一切与电脑无关的"
        "复杂任务。用户提出越界请求时，温和说明你只是个小管家并给出可行方向，不硬接。\n\n"
        "工具使用纪律：\n"
        "* 能只读就不写；写文件/执行命令前先用一句话说明目的；\n"
        "* 一次只做一小步；重要删除动作必须先复述目标再执行；\n"
        "* `monitor_query` 可以查用户今天的活动日志——用户问\"今天干了什么\"时用它；\n"
        "* 生成「本日工作记录」时：查 `monitor_query` 拿时间线与键入摘录，"
        "按时间顺序归纳，输出包含：时间线概览、关键工作、产出、待办线索四节。\n\n"
        "当前时间：%1。");
    prompt = prompt.arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm ddd")));

    const QString appendix = Settings::instance().assistantPersonaAppendix().trimmed();
    if (!appendix.isEmpty())
        prompt += QStringLiteral("\n用户补充设定：\n%1\n").arg(appendix);

    // 瞬时工作记忆 + 长期记忆索引（引用层叠：只注入索引，正文按需检索）
    if (m_work) {
        const QString tasks = m_work->promptBlock();
        if (!tasks.isEmpty())
            prompt += QStringLiteral("\n%1\n").arg(tasks);
    }
    if (m_memory) {
        prompt += QStringLiteral(
            "\n长期记忆索引（细节用 `memory_search` 查）：\n%1\n")
                      .arg(m_memory->indexSummary());
    }
    return prompt;
}

void GoodyModule::sendMessage(const QString &text)
{
    if (m_client->busy())
        return;
    // 工具执行期禁发新消息（busy 只覆盖请求在途，不覆盖工具执行与确认
    // 等待）：此时 assistant(tool_calls) 与其 tool 结果尚未配对完成，新
    // user 消息插进中间会让请求序列非法（API 400），旧工具结果还会以
    // 新回合的名义交错执行。守卫分支必须给用户可见反馈——文字已从输入
    // 框清空，静默 return 会让用户无感丢失（B-3）
    if (m_toolPhase) {
        if (m_window)
            m_window->showErrorLine(QStringLiteral("工具执行中，请等当前操作完成后再发送"));
        return;
    }
    // 统一懒建窗口（原实现 Key 缺失分支与正常路径各调一次，提前共用；
    // ensureWindow 幂等，两分支本就都会建窗）
    ensureWindow();
    if (Settings::instance().assistantApiKey().trimmed().isEmpty()) {
        m_window->showErrorLine(
            QStringLiteral("还没有配置 API Key：设置 → Goody 管家 → 填入 DeepSeek API Key。"));
        return;
    }
    m_toolRounds = 0;
    m_window->appendUserBubble(text);
    // T-070 dsh 化：会话即事件日志——回合边界与用户消息都是持久事件
    m_session->turnStart();
    m_session->appendUserMessage(text);
    m_session->saveSoon();
    if (auto *p = MainThreadProbe::instance())
        p->mark("goody_send");
    runTurn();
}

// T-050-P2：会话切换/新建/删除与消息四件套共用的回合锁守卫（busy =
// LLM 请求在途，工具期 = tool_calls 回填/确认等待）。中途执行这些域
// 操作会让在途回包写错会话（等价 C-N2 迟到回包场景），一律拒绝并给
// 用户可见反馈
bool GoodyModule::turnLocked() const
{
    if (m_client->busy() || m_toolPhase) {
        if (m_window)
            m_window->showErrorLine(QStringLiteral("回复进行中，请等当前回合结束后再操作"));
        return true;
    }
    return false;
}

// 发起一轮 LLM 请求：快照回合代数 → busy 灯/流式气泡 → 注入系统提示词与
// 工具 schema；chat 返回 false 属不可能路径，防御性复位
void GoodyModule::runTurn()
{
    m_activeTurnEpoch = m_toolEpoch; // 回合代数快照：清空后迟到回包按失配丢弃
    m_window->setBusy(true);
    m_window->beginAssistantBubble();
    if (auto *p = MainThreadProbe::instance())
        p->mark("goody_turn_begin");
    m_session->setSystemPrompt(buildSystemPrompt());
    const Settings &s = Settings::instance();
    const bool ok = m_client->chat(s.assistantApiBaseUrl(), s.assistantApiKey(),
                                   s.assistantApiModel(), m_session->messages(),
                                   m_registry->toolSchemasForApi());
    if (!ok) {
        // busy 状态下不会发生，防御性复位。T-062 修复: 原先只复位 busy 灯，
        // 漏收口 beginAssistantBubble 开出的流式气泡——m_streaming 悬挂 true
        //（finishAssistantBubble 幂等，空文本即收口不画空壳）
        m_window->setBusy(false);
        m_window->finishAssistantBubble(QString(), QString());
    }
    updateWorkingState(); // T-050：请求在途 → 呼吸光效起
}

// 回合收束四分支：代数失配（迟到回包，仅复位窗口）→ 失败/中止（剥
// tool_calls 入会话）→ 普通回答（收束）→ tool_calls（轮次计数后进工具执行期）
void GoodyModule::onTurnFinished(bool ok, const QJsonObject &assistantMessage,
                                 const QString &error)
{
    // 迟到回包丢弃（C-N2）：回包所属回合的会话已被清空（代数失配）时，
    // 不入会话、不走工具链——清空后 unconditional append 会让新会话以
    // assistant（含 tool_calls 时更糟）开头，下一轮请求被网关 400 且无自愈。
    // 只做窗口收尾复位 busy 灯，用户可立即发起新会话。
    // 安全前提：会话清空已触发窗口 reload（sessionCleared → m_session->clear()
    // 发 changed → GoodyWindow 重建气泡），m_streamBubble 已置空——下方
    // finishAssistantBubble 在此只复位流式状态，不会把迟到内容画进新会话
    //（若解除该依赖须改走丢弃路径）
    if (m_activeTurnEpoch != m_toolEpoch) {
        if (m_window) {
            m_window->setBusy(false);
            m_window->finishAssistantBubble(
                assistantMessage.value(QStringLiteral("content")).toString(),
                assistantMessage.value(QStringLiteral("reasoning")).toString());
        }
        updateWorkingState(); // T-050：迟到回包收束（busy 已复位）
        return;
    }

    // 会话一致性护栏（P0：孤儿 tool_calls 会让之后每轮请求被 API 整包
    // 拒绝且永久卡死）：失败/中止路径不会执行工具——入会话前把 blocks 里
    // 的 tool_call 块剥除（保留 text/reasoning 正文，维持上下文连续），
    // 对齐 dsh"中断把已流出前缀定格为 assistant 消息"
    QJsonArray blocks = assistantMessage.value(QStringLiteral("blocks")).toArray();
    if (!ok) {
        QJsonArray kept;
        for (const QJsonValue &b : blocks) {
            if (b.toObject().value(QStringLiteral("t")).toString()
                != QLatin1String(asst_blocks::kToolCall))
                kept.append(b);
        }
        blocks = kept;
    }
    // finish 判据：正常取流内 finish_reason；中止/失败改写（投影不消费，
    // 供调试与未来 UI 徽章）
    QString finish = assistantMessage.value(QStringLiteral("finish")).toString();
    if (!ok)
        finish = error == QStringLiteral("已中止") ? QStringLiteral("aborted")
                                                   : QStringLiteral("error");
    m_session->appendAssistantBlocks(blocks, finish);
    m_session->saveSoon();

    if (m_window) {
        m_window->setBusy(false);
        // 先入会话再定稿：定稿后的"重新生成"判定需要会话里已有本条回复
        //（T-070 块化：正文/思考从 blocks 拼回）
        QString bubbleText, bubbleReasoning;
        for (const QJsonValue &b : blocks) {
            const QJsonObject blk = b.toObject();
            const QString bt = blk.value(QStringLiteral("t")).toString();
            if (bt == QLatin1String(asst_blocks::kText))
                bubbleText += blk.value(QStringLiteral("text")).toString();
            else if (bt == QLatin1String(asst_blocks::kReasoning))
                bubbleReasoning += blk.value(QStringLiteral("text")).toString();
        }
        m_window->finishAssistantBubble(bubbleText, bubbleReasoning);
        if (!ok && error != QStringLiteral("已中止"))
            m_window->showErrorLine(QStringLiteral("请求失败：%1").arg(error));
    }

    if (!ok) {
        m_toolPhase = false; // 中止收束工具执行期守卫
        if (m_window)
            m_window->setToolPhase(false);
        // T-070：失败/中止收束回合（dsh turn/end reason 语义）
        m_session->turnEnd(error == QStringLiteral("已中止") ? QStringLiteral("aborted")
                                                             : QStringLiteral("error"));
        m_session->saveSoon();
        updateWorkingState(); // T-050：失败/中止收束
        return; // 失败/中止不再走工具链
    }

    // T-070：max-tokens 粘性收束——length 截断的 tool_calls 参数可能残缺
    // （JSON 不完整），不执行；有 tool_calls 时补错误 tool/result 防孤儿
    if (finish == QLatin1String("length")) {
        for (const QJsonValue &b : blocks) {
            const QJsonObject blk = b.toObject();
            if (blk.value(QStringLiteral("t")).toString()
                == QLatin1String(asst_blocks::kToolCall)) {
                m_session->appendToolResult(
                    blk.value(QStringLiteral("id")).toString(),
                    QStringLiteral("{\"ok\":false,\"error\":\"生成因长度截断，调用未执行\"}"),
                    true);
            }
        }
        m_session->turnEnd(QStringLiteral("max-tokens"));
        m_session->saveSoon();
        m_toolPhase = false;
        if (m_window) {
            m_window->setToolPhase(false);
            m_window->showErrorLine(QStringLiteral("回复因长度截断，已停止本轮工具调用。"));
        }
        updateWorkingState();
        return;
    }

    const QJsonArray toolCalls = assistantMessage.value(QStringLiteral("tool_calls")).toArray();
    if (toolCalls.isEmpty()) {
        m_toolPhase = false; // 普通回答，回合结束
        if (m_window)
            m_window->setToolPhase(false);
        m_session->turnEnd(QStringLiteral("completed"));
        m_session->saveSoon();
        updateWorkingState(); // T-050：回合结束
        return;
    }

    if (++m_toolRounds > kMaxToolRounds) {
        // 轮次超限收尾：本轮 tool_calls 已随 assistant 块入会话，必须
        // 补齐 tool 结果再结束（否则留下非法序列，会话被永久拒绝）
        for (const QJsonValue &tc : toolCalls) {
            m_session->appendToolResult(
                tc.toObject().value(QStringLiteral("id")).toString(),
                QStringLiteral("{\"ok\":false,\"error\":\"工具调用轮次已达上限，已停止执行\"}"),
                true);
        }
        m_session->turnEnd(QStringLiteral("blocked"));
        m_session->saveSoon();
        m_toolPhase = false;
        if (m_window) {
            m_window->setToolPhase(false);
            m_window->showErrorLine(QStringLiteral("工具调用轮次过多（>%1），已停止。")
                                        .arg(kMaxToolRounds));
        }
        updateWorkingState(); // T-050：轮次超限收束
        return;
    }
    m_toolPhase = true; // 进入工具执行期（sendMessage 守卫到结果回填完毕）
    if (m_window)
        m_window->setToolPhase(true); // 工具期禁用「清空」（防孤儿 tool 序列）
    m_pendingToolCalls = toolCalls;
    updateWorkingState(); // T-050：工具执行期 → 呼吸光效保持
    processNextToolCall();
}

// 工具执行队列泵：队空则续轮生成；队首取权 → 权限决策 → Allow 直执行 /
// 否则内联确认卡片（回调带 epoch 防会话清空后回填）
void GoodyModule::processNextToolCall()
{
    if (m_pendingToolCalls.isEmpty()) {
        runTurn(); // 全部工具结果已入会话 → 下一轮生成
        return;
    }
    const QJsonObject tc = m_pendingToolCalls.first().toObject();
    m_pendingToolCalls.removeAt(0); // QJsonArray 无 takeFirst：取首 + 移除两步走
    const QString id = tc.value(QStringLiteral("id")).toString();
    const QJsonObject fn = tc.value(QStringLiteral("function")).toObject();
    // 模型回包的是转写名（fs_read）：还原内部名（fs.read）再执行
    const QString name = m_registry->fromApiName(fn.value(QStringLiteral("name")).toString());
    const QJsonObject args =
        QJsonDocument::fromJson(fn.value(QStringLiteral("arguments")).toString().toUtf8()).object();

    if (m_window)
        m_window->toolRowStarted(id, name, summarizeToolArgs(args)); // T-070 过程行

    // T-070 dsh 化：审批是会话日志里的审计事件对（approval/asked → decided），
    // 可重放可审计；fail-closed——任何异常路径一律拒绝
    const PermissionManager::Decision decision = m_perm->decide(name, args);
    if (decision == PermissionManager::Decision::Allow) {
        m_session->appendApprovalAsked(id, name);
        m_session->appendApprovalDecided(id, QStringLiteral("allowed-auto"));
        m_session->saveSoon();
        executeToolCall(id, name, args);
        return;
    }
    // 逐条确认：内联确认卡片（Ask 与 Smart 白名单外共用此路径）
    m_session->appendApprovalAsked(id, name);
    m_perm->audit(name, args, QStringLiteral("confirm"), false);
    if (!m_window) {
        m_session->appendApprovalDecided(id, QStringLiteral("unavailable"));
        m_session->saveSoon();
        toolCallDone(id, QJsonObject{{"ok", false},
                                     {"error", QStringLiteral("无聊天窗交互，已拒绝")}});
        return;
    }
    const qint64 epoch = m_toolEpoch; // 会话清空收束代数：失配即过期回调
    m_window->promptPermission(name, summarizeToolArgs(args),
                               [this, id, name, args, epoch](bool allowed, bool always) {
                                   if (epoch != m_toolEpoch)
                                       return; // 会话已清空收束：丢弃，不回填结果
                                   if (always && allowed) {
                                       m_perm->grantAlways(name);
                                       Settings::instance().setAssistantAlwaysAllowTools(
                                           m_perm->alwaysAllowTools()); // 持久化"总是允许"
                                   }
                                   if (!allowed) {
                                       m_perm->audit(name, args, QStringLiteral("denied"), false);
                                       m_session->appendApprovalDecided(id, QStringLiteral("rejected"));
                                       m_session->saveSoon();
                                       toolCallDone(id, QJsonObject{
                                                            {"ok", false},
                                                            {"error", QStringLiteral("用户拒绝了本次调用")}});
                                       return;
                                   }
                                   m_session->appendApprovalDecided(id, always
                                                                            ? QStringLiteral("allowed-always")
                                                                            : QStringLiteral("allowed-once"));
                                   m_session->saveSoon();
                                   executeToolCall(id, name, args);
                               });
}

void GoodyModule::executeToolCall(const QString &toolCallId, const QString &name,
                                  const QJsonObject &args)
{
    m_perm->audit(name, args, QStringLiteral("allowed"), true);
    const qint64 epoch = m_toolEpoch; // 会话清空收束代数：失配即过期回调
    m_registry->invoke(name, args, [this, toolCallId, epoch](const QJsonObject &result) {
        if (epoch != m_toolEpoch)
            return; // 会话已清空收束：在途结果丢弃（避免孤儿 tool 入会话）
        toolCallDone(toolCallId, result);
    });
}

void GoodyModule::toolCallDone(const QString &toolCallId, const QJsonObject &result)
{
    // T-070：工具结果入会话事件日志（content JSON 串 + isError 双通道）
    m_session->appendToolResult(toolCallId,
                                QString::fromUtf8(
                                    QJsonDocument(result).toJson(QJsonDocument::Compact)),
                                !result.value(QStringLiteral("ok")).toBool(true));
    m_session->saveSoon(); // 防抖：工具密集回合不再每个结果全量写盘
    if (m_window)
        m_window->toolRowFinished(toolCallId,
                                  result.value(QStringLiteral("ok")).toBool(true),
                                  summarizeToolResult(result)); // T-070 过程行收束
    if (m_pendingToolCalls.isEmpty() && m_toolPhase) {
        m_toolPhase = false; // 结果全部回填，工具执行期结束
        if (m_window)
            m_window->setToolPhase(false);
    }
    processNextToolCall();
    updateWorkingState(); // T-050：链条同步走完（下一轮 runTurn 或收束）后再广播
}

} // namespace poptools
