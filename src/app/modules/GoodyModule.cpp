#include "GoodyModule.h"

#include <QAction>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>

#include "../AppServices.h"
#include "core/assistant/ChatSession.h"
#include "core/assistant/DailyRecordJob.h"
#include "core/assistant/McpStdioClient.h"
#include "core/assistant/MemoryStore.h"
#include "core/assistant/Scheduler.h"
#include "core/assistant/WorkingMemory.h"
#include "core/monitor/ActivityStore.h"
#include "core/assistant/FsTool.h"
#include "core/assistant/LlmClient.h"
#include "core/assistant/MonitorQueryTool.h"
#include "core/assistant/PermissionManager.h"
#include "core/assistant/ShellTool.h"
#include "core/assistant/ToolRegistry.h"
#include "core/hotkey/GlobalHotKey.h"
#include "core/hotkey/HotKeyNames.h"
#include "core/settings/Settings.h"
#include "ui/goody/GoodyWindow.h"

namespace poptools {

namespace {
constexpr int kMaxToolRounds = 8; // 单次用户消息最多 8 轮工具往返（防失控）
} // namespace

GoodyModule::GoodyModule(QObject *parent)
    : QObject(parent)
{
}

void GoodyModule::init(AppServices &services)
{
    m_services = &services;
    m_registry = services.toolRegistry;
    Q_ASSERT(m_registry && "组合根必须先创建共享 ToolRegistry 再装配模块");

    // 审计存储（goody 目录；与 monitor 目录的 ActivityStore 各自独立线程）
    const QString goodyDir = QStandardPaths::writableLocation(
                                 QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/goody");
    m_auditStore = new ActivityStore(goodyDir, this);
    m_auditStore->start();

    m_perm = new PermissionManager(m_auditStore, this);
    m_perm->setToolRegistry(m_registry); // readOnly 判定以注册表为准（app.* 不再一刀切放行）
    m_client = new LlmClient(this);
    m_session = new ChatSession(goodyDir + QStringLiteral("/chat/sessions.json"), this);
    m_session->load();
    m_memory = new MemoryStore(goodyDir, this);
    m_memory->load();
    m_work = new WorkingMemory(goodyDir, this);

    registerBuiltinTools();
    setupMcp();

    // ---- 本日工作记录：启动补账 + 每日到点 + 工具手动触发 ----
    const QString monitorDir = QStandardPaths::writableLocation(
                                   QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/monitor");
    m_recordJob = new DailyRecordJob(monitorDir, goodyDir, this);
    m_scheduler = new Scheduler(this);
    applySummarySchedule();
    // 启动补账（延迟 5s：等事件循环与网络就绪；只补历史日，今天留给定时点）
    if (Settings::instance().assistantAutoSummarize()) {
        QTimer::singleShot(5000, this, [this] {
            if (Settings::instance().assistantAutoSummarize())
                m_recordJob->processPending(false, [](int, const QString &) {});
        });
    }

    // 呼出热键（注册失败由组合根统一气泡告知；R5-D2：注册返回值用于
    // 托盘文案标注，不再静默）
    const quint32 hkVk = Settings::instance().assistantHotKeyVk();
    const quint32 hkMods = Settings::instance().assistantHotKeyModifiers();
    const bool hkOk = services.registerHotKey(hkVk, hkMods,
                                              [this] { openChat(); });

    // R5-D2：托盘文案的快捷键改为按设置动态生成（ini 改 assistantHotKeyVk
    // 后不再与事实不符）；注册失败时追加标注（CaptureModule 的"已被占用"
    // 托盘文案先例）
    // R5-D2 复审修正（P3-4）：失败标注对齐 CaptureModule 的括注先例风格，
    // 原因取 GlobalHotKey::lastError 的具体原因——常见即"已被其他程序占用"，
    // 进程内冲突/Win32 错误码场景也比笼统"注册失败"可排查
    const QString hkFailNote =
        hkOk ? QString()
             : QStringLiteral("（%1）").arg(
                   services.hotkey && !services.hotkey->lastError().isEmpty()
                       ? services.hotkey->lastError()
                       : QStringLiteral("注册失败"));
    m_trayAction = services.addTrayAction(
        QStringLiteral("Goody 管家 (%1)%2").arg(hotKeyDisplayName(hkVk, hkMods), hkFailNote),
        [this] { openChat(); });

    // 聊天数据流
    connect(m_client, &LlmClient::contentDelta, this, [this](const QString &delta) {
        if (m_window)
            m_window->appendAssistantDelta(delta);
    });
    connect(m_client, &LlmClient::finished, this, &GoodyModule::onTurnFinished);

    connect(&services, &AppServices::settingsChanged, this, &GoodyModule::onSettingsChanged);
}

void GoodyModule::shutdown()
{
    if (m_session)
        m_session->save();
    if (m_auditStore)
        m_auditStore->stop();
}

void GoodyModule::onSettingsChanged()
{
    // 权限配置即时重读（模式/白名单在设置页修改后生效）
    if (m_perm) {
        const Settings &s = Settings::instance();
        m_perm->setMode(PermissionManager::modeFromSetting(s.assistantPermMode()));
        m_perm->setAlwaysAllowTools(s.assistantAlwaysAllowTools());
        m_perm->setShellAllowPrefixes(s.assistantShellAllowPrefixes());
    }
    // 总结调度接线：总结时刻/自动开关改动即时生效（原实现须重启）
    applySummarySchedule();
}

// 总结调度接线（init 与设置变更共用）：按当前设置注册/启停每日任务
void GoodyModule::applySummarySchedule()
{
    if (!m_scheduler || !m_recordJob)
        return;
    const Settings &s = Settings::instance();
    if (!s.assistantAutoSummarize()) {
        m_scheduler->stop();
        return;
    }
    m_scheduler->setDailyTask(
        QStringLiteral("dailySummary"),
        QTime::fromString(s.assistantSummaryTime(), QStringLiteral("HH:mm")),
        [this] { m_recordJob->processPending(true, [this](int n, const QString &err) {
                     if (n > 0 && m_services && m_services->notify)
                         m_services->notify(QStringLiteral("Goody 管家"),
                                            QStringLiteral("已生成 %1 份本日工作记录")
                                                .arg(n));
                     Q_UNUSED(err);
                 }); });
    m_scheduler->start();
}

void GoodyModule::registerBuiltinTools()
{
    if (!m_registry->has(QStringLiteral("time.now"))) {
        ToolDef d;
        d.name = QStringLiteral("time.now");
        d.readOnly = true;
        d.description = QStringLiteral("获取当前日期时间（本地时区）。");
        d.parameters = QJsonObject{{"type", QStringLiteral("object")},
                                   {"properties", QJsonObject{}}};
        d.fn = [](const QJsonObject &, const std::function<void(QJsonObject)> &done) {
            const QDateTime now = QDateTime::currentDateTime();
            done(QJsonObject{{"ok", true},
                             {"datetime", now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss ddd"))},
                             {"epochMs", double(now.toMSecsSinceEpoch())}});
        };
        m_registry->registerTool(d);
    }

    for (const ToolDef &d : makeFsTools())
        m_registry->registerTool(d);
    m_registry->registerTool(makeShellRunTool());
    if (m_memory && m_work) {
        for (const ToolDef &d : makeMemoryTools(m_memory, m_work))
            m_registry->registerTool(d);
    }
    if (m_services) {
        const QString monitorDir = QStandardPaths::writableLocation(
                                       QStandardPaths::AppLocalDataLocation)
            + QStringLiteral("/monitor");
        m_registry->registerTool(makeMonitorQueryTool(monitorDir, this));

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
    }
}

// mcp.json 服务器拉起：就绪后 listTools → 注册 mcp__<server>__<tool>。
// MCP 是可选能力：无配置文件即零开销
void GoodyModule::setupMcp()
{
    const QString goodyDir = QStandardPaths::writableLocation(
                                 QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/goody");
    const QList<McpStdioClient::ServerConfig> servers = McpStdioClient::loadConfig(goodyDir);
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
                        d.parameters = QJsonObject{{"type", QStringLiteral("object")},
                                                   {"properties", QJsonObject{}}};
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
        client->start();
    }
}

void GoodyModule::ensureWindow()
{
    if (m_window)
        return;
    m_window = new GoodyWindow(); // 无父窗口：随 QApplication 退出清理
    m_window->restoreGeometry(Settings::instance().assistantGeometry());
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
            if (m_window)
                m_window->showErrorLine(QStringLiteral("工具执行已随会话清空中止"));
        }
        // busy 期清空（C-N2）：abort 在途 LLM 请求——其 finished 回包带旧代数
        // 到达，onTurnFinished 丢弃（不 append、不走工具链）。窗口侧 busy 已
        // 禁用清空按钮，这是绕过 UI 的兜底路径
        if (m_client->busy())
            m_client->abort();
        m_session->clear();
        m_session->save();
    });
    // 关窗记忆几何（hideEvent 通知，见 GoodyWindow）
    connect(m_window, &GoodyWindow::windowHidden, this, [this] {
        Settings::instance().setAssistantGeometry(m_window->saveGeometry());
    });
    connect(m_window, &QObject::destroyed, this, [this] { m_window = nullptr; });
}

void GoodyModule::openChat()
{
    ensureWindow();
    if (m_window->isVisible()) {
        m_window->raise();
        m_window->activateWindow();
        return;
    }
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
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

QString GoodyModule::summarizeArgs(const QJsonObject &args) const
{
    const QString raw = QString::fromUtf8(
        QJsonDocument(args).toJson(QJsonDocument::Compact));
    return raw.size() > 200 ? raw.left(200) + QStringLiteral("…") : raw;
}

QString GoodyModule::summarizeResult(const QJsonObject &result) const
{
    if (!result.value(QStringLiteral("ok")).toBool(true)) {
        const QString err = result.value(QStringLiteral("error")).toString();
        return err.isEmpty() ? QStringLiteral("执行失败") : err;
    }
    QString summary;
    for (auto it = result.constBegin(); it != result.constEnd(); ++it) {
        if (it.key() == QLatin1String("ok"))
            continue;
        // 字符串原样；其他类型序列化时去掉数组包装（QJsonValue 无独立序列化，
        // 套 [..] 后剥壳，避免显示成 {"v":...} 这种多余的包装结构）
        QString rendered;
        if (it.value().isString()) {
            rendered = it.value().toString();
        } else {
            rendered = QString::fromUtf8(
                           QJsonDocument(QJsonArray{it.value()}).toJson(QJsonDocument::Compact))
                           .trimmed();
            rendered = rendered.mid(1, rendered.size() - 2); // 剥 [ ]
        }
        summary += QStringLiteral("%1=%2 ").arg(it.key(), rendered);
        if (summary.size() > 160) {
            summary += QStringLiteral("…");
            break;
        }
    }
    return summary.trimmed();
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
    if (Settings::instance().assistantApiKey().trimmed().isEmpty()) {
        ensureWindow();
        m_window->showErrorLine(
            QStringLiteral("还没有配置 API Key：设置 → Goody 管家 → 填入 DeepSeek API Key。"));
        return;
    }
    ensureWindow();
    m_toolRounds = 0;
    m_window->appendUserBubble(text);
    m_session->append(QJsonObject{{"role", QStringLiteral("user")}, {"content", text}});
    m_session->saveSoon();
    runTurn();
}

void GoodyModule::runTurn()
{
    m_activeTurnEpoch = m_toolEpoch; // 回合代数快照：清空后迟到回包按失配丢弃
    m_window->setBusy(true);
    m_window->beginAssistantBubble();
    m_session->setSystemPrompt(buildSystemPrompt());
    const Settings &s = Settings::instance();
    const bool ok = m_client->chat(s.assistantApiBaseUrl(), s.assistantApiKey(),
                                   s.assistantApiModel(), m_session->messages(),
                                   m_registry->toolSchemasForApi());
    if (!ok)
        m_window->setBusy(false); // busy 状态下不会发生，防御性复位
}

void GoodyModule::onTurnFinished(bool ok, const QJsonObject &assistantMessage,
                                 const QString &error)
{
    // 迟到回包丢弃（C-N2）：回包所属回合的会话已被清空（代数失配）时，
    // 不入会话、不走工具链——清空后 unconditional append 会让新会话以
    // assistant（含 tool_calls 时更糟）开头，下一轮请求被网关 400 且无自愈。
    // 只做窗口收尾复位 busy 灯，用户可立即发起新会话
    if (m_activeTurnEpoch != m_toolEpoch) {
        if (m_window) {
            m_window->setBusy(false);
            m_window->finishAssistantBubble(
                assistantMessage.value(QStringLiteral("content")).toString());
        }
        return;
    }

    if (m_window) {
        m_window->setBusy(false);
        m_window->finishAssistantBubble(assistantMessage.value(QStringLiteral("content")).toString());
        if (!ok && error != QStringLiteral("已中止"))
            m_window->showErrorLine(QStringLiteral("请求失败：%1").arg(error));
    }

    // 会话一致性护栏（P0：孤儿 tool_calls 会让之后每轮请求被 API 整包
    // 拒绝且永久卡死）：失败/中止路径的消息可能带着已聚合的半截
    // tool_calls，且不会再有工具执行——入会话前剥离 tool_calls 字段
    //（保留正文，维持上下文连续）
    QJsonObject msg = assistantMessage;
    if (!ok)
        msg.remove(QStringLiteral("tool_calls"));
    // 本地中止标记（LlmClient 插入，无任何消费方）不随会话回传：网关对
    // message 内非标准字段的容忍度无文档承诺，剥除后请求体只含标准字段
    msg.remove(QStringLiteral("aborted"));
    m_session->append(msg);
    m_session->saveSoon();

    if (!ok) {
        m_toolPhase = false; // 中止收束工具执行期守卫
        if (m_window)
            m_window->setToolPhase(false);
        return; // 失败/中止不再走工具链
    }

    const QJsonArray toolCalls = assistantMessage.value(QStringLiteral("tool_calls")).toArray();
    if (toolCalls.isEmpty()) {
        m_toolPhase = false; // 普通回答，回合结束
        if (m_window)
            m_window->setToolPhase(false);
        return;
    }

    if (++m_toolRounds > kMaxToolRounds) {
        // 轮次超限收尾：本轮 tool_calls 已随 assistant 消息入会话，必须
        // 补齐 tool 结果再结束（否则留下非法序列，会话被永久拒绝）
        for (const QJsonValue &tc : toolCalls) {
            m_session->appendToolResult(
                tc.toObject().value(QStringLiteral("id")).toString(),
                QJsonObject{{"ok", false},
                            {"error", QStringLiteral("工具调用轮次已达上限，已停止执行")}});
        }
        m_session->saveSoon();
        m_toolPhase = false;
        if (m_window) {
            m_window->setToolPhase(false);
            m_window->showErrorLine(QStringLiteral("工具调用轮次过多（>%1），已停止。")
                                        .arg(kMaxToolRounds));
        }
        return;
    }
    m_toolPhase = true; // 进入工具执行期（sendMessage 守卫到结果回填完毕）
    if (m_window)
        m_window->setToolPhase(true); // 工具期禁用「清空」（防孤儿 tool 序列）
    m_pendingToolCalls = toolCalls;
    processNextToolCall();
}

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
        m_window->appendSystemLine(
            QStringLiteral("⚙ 调用工具 %1(%2)").arg(name, summarizeArgs(args)));

    const PermissionManager::Decision decision = m_perm->decide(name, args);
    if (decision == PermissionManager::Decision::Allow) {
        executeToolCall(id, name, args);
        return;
    }
    // 逐条确认：内联确认卡片（Ask 与 Smart 白名单外共用此路径）
    m_perm->audit(name, args, QStringLiteral("confirm"), false);
    if (!m_window) {
        toolCallDone(id, QJsonObject{{"ok", false},
                                     {"error", QStringLiteral("无聊天窗交互，已拒绝")}});
        return;
    }
    const qint64 epoch = m_toolEpoch; // 会话清空收束代数：失配即过期回调
    m_window->promptPermission(name, summarizeArgs(args),
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
                                       toolCallDone(id, QJsonObject{
                                                            {"ok", false},
                                                            {"error", QStringLiteral("用户拒绝了本次调用")}});
                                       return;
                                   }
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
    m_session->appendToolResult(toolCallId, result);
    m_session->saveSoon(); // 防抖：工具密集回合不再每个结果全量写盘
    if (m_window) {
        m_window->appendSystemLine(
            (result.value(QStringLiteral("ok")).toBool(true)
                 ? QStringLiteral("✓ 完成：%1")
                 : QStringLiteral("✗ 失败：%1"))
                .arg(summarizeResult(result)));
    }
    if (m_pendingToolCalls.isEmpty() && m_toolPhase) {
        m_toolPhase = false; // 结果全部回填，工具执行期结束
        if (m_window)
            m_window->setToolPhase(false);
    }
    processNextToolCall();
}

} // namespace poptools
