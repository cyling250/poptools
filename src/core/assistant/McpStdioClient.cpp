#include "McpStdioClient.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSet>
#include <QTimer>

namespace poptools {

namespace {
constexpr int kRequestTimeoutMs = 30 * 1000;
constexpr const char *kProtocolVersion = "2024-11-05";
} // namespace

QList<McpStdioClient::ServerConfig> McpStdioClient::loadConfig(const QString &goodyDir)
{
    QList<ServerConfig> out;
    QSet<QString> seen;
    QFile f(goodyDir + QStringLiteral("/mcp.json"));
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QJsonArray servers = QJsonDocument::fromJson(f.readAll())
                                   .object()
                                   .value(QStringLiteral("servers"))
                                   .toArray();
    for (const QJsonValue &v : servers) {
        const QJsonObject o = v.toObject();
        ServerConfig c;
        c.name = o.value(QStringLiteral("name")).toString().trimmed();
        c.command = o.value(QStringLiteral("command")).toString().trimmed();
        for (const QJsonValue &a : o.value(QStringLiteral("args")).toArray())
            c.args << a.toString();
        if (!c.name.isEmpty() && !c.command.isEmpty()) {
            // T-062 修复: 重复 server 名会让 mcp__<name>__<tool> 工具名相同，
            // registerTool 覆盖语义下后者静默顶掉前者（工具清单只见其一）。
            // 重名条目跳过并告警（配置容错口径内，不改文件格式）
            if (seen.contains(c.name)) {
                qWarning("MCP 配置：server 名重复，跳过后者：%s", qPrintable(c.name));
                continue;
            }
            seen.insert(c.name);
            out << c; // 条目不全的跳过（配置容错）
        }
    }
    return out;
}

McpStdioClient::McpStdioClient(const ServerConfig &cfg, QObject *parent)
    : QObject(parent)
    , m_cfg(cfg)
{
}

McpStdioClient::~McpStdioClient()
{
    stop();
}

void McpStdioClient::start()
{
    if (m_proc)
        return;
    // T-062 修复: R2-F2 —— 重试路径清残留半行缓冲：旧进程被 kill 时 stdout
    // 半行可能残留在 m_buf，与新进程首个数据块拼接会损坏新会话首行（恰为
    // initialize 应答），再吃一轮 30s 超时才自愈。基线 start() 仅调一次
    // 不可达；F1 开启重试后可触发
    m_buf.clear();
    m_handshakeDone = false; // 新连接握手重来
    m_proc = new QProcess(this);
    m_proc->setProgram(m_cfg.command);
    m_proc->setArguments(m_cfg.args);
    connect(m_proc, &QProcess::readyRead, this, [this] {
        m_buf += m_proc->readAllStandardOutput();
        int nl = -1;
        while ((nl = m_buf.indexOf('\n')) >= 0) {
            const QByteArray line = m_buf.left(nl).trimmed();
            m_buf.remove(0, nl + 1);
            if (!line.isEmpty())
                onStdoutLine(line);
        }
    });
    // 子进程死亡：清空在途请求（错误语义），ready 回退。
    // T-062 修复: R2-F1 —— 握手未完成（m_handshakeDone==false）时补 stop()：
    // 原实现 m_proc 残留非空，start() 幂等挡死重试，启动失败/崩溃类失败
    // 永久哑火（F1 的超时重试只覆盖三类失败中的一类）。stop() 置 m_proc 空
    // 使下次 openChat 可重入；其内 failPending 此时表已空（本分支已先清），
    // 不会以"MCP 已停止"覆盖"进程异常退出"的错误语义。ready 后进程死亡
    // （m_handshakeDone==true）不 stop，维持基线"死后不自动重启"语义
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_ready) {
            m_ready = false;
            emit readyChanged();
        }
        failPending(QStringLiteral("MCP 进程异常退出"));
        if (!m_handshakeDone)
            stop();
    });
    connect(m_proc, &QProcess::finished, this, [this] {
        failPending(QStringLiteral("MCP 进程已退出"));
        if (!m_handshakeDone)
            stop();
    });
    m_proc->start();

    // 握手：initialize → 应答后发 initialized 通知 → ready
    QJsonObject req{
        {"jsonrpc", QStringLiteral("2.0")},
        {"id", m_nextId},
        {"method", QStringLiteral("initialize")},
        {"params", QJsonObject{
                       {"protocolVersion", QString::fromUtf8(kProtocolVersion)},
                       {"capabilities", QJsonObject{}},
                       {"clientInfo",
                        QJsonObject{{"name", QStringLiteral("PopTools-Goody")},
                                    {"version", QStringLiteral("0.1")}}}}},
    };
    const int id = m_nextId++;
    m_pending.insert(id, [this](const QJsonObject &result) {
        // T-062 修复: R2-F1 —— 错误应答提前返回：failPending 以
        // {"error":{…}} 形状回调（进程 FailedToStart/崩溃/握手前退出），
        // 原回调 Q_UNUSED(result) 不看错误对象，一路 send+ready 造成伪
        // ready——readyChanged→listTools 写死进程缓冲，30s 后报"请求超时"，
        // 真因被掩盖。错误路径不置 ready、不发 initialized 通知，qWarning
        // 留真因（进程死亡场景由 errorOccurred/finished 分支的 stop 收口重试）
        if (result.contains(QStringLiteral("error"))) {
            qWarning("MCP[%s]: initialize 失败：%s",
                     qPrintable(m_cfg.name),
                     qPrintable(result.value(QStringLiteral("error"))
                                    .toObject()
                                    .value(QStringLiteral("message"))
                                    .toString()));
            return;
        }
        // initialized 是通知（无 id，不应答）
        QJsonObject note{{"jsonrpc", QStringLiteral("2.0")},
                         {"method", QStringLiteral("notifications/initialized")}};
        send(note);
        m_handshakeDone = true;
        m_ready = true;
        emit readyChanged();
    });
    send(req);
    // 握手看门狗：30s 无应答判失败。T-062 修复: 原先仅清 pending——npx 冷
    // 启动（首次下载依赖）常超 30s，超时后进程照常常驻（装完就绪却无人
    // 消费），m_ready 恒 false 且 m_mcpStarted 一次性置位永无重试，MCP 永久
    // 哑火。现在超时即 stop() 杀进程回收，GoodyModule::ensureMcpStarted 在
    // 下次 openChat 时可重入 start() 重试（start 幂等，进程在手即返回；
    // 无阻塞等待、无线程变更）
    QTimer::singleShot(kRequestTimeoutMs, this, [this, id] {
        if (m_pending.contains(id)) {
            m_pending.remove(id);
            qWarning("MCP[%s]: initialize 握手超时，已停止子进程（下次打开聊天窗重试）",
                     qPrintable(m_cfg.name));
            stop();
        }
    });
}

// 停止：kill 子进程 + deleteLater（QProcess 析构兜底终止），在途请求统一
// 按"MCP 已停止"失败回调
void McpStdioClient::stop()
{
    if (!m_proc)
        return;
    m_proc->kill();
    m_proc->deleteLater();
    m_proc = nullptr;
    m_ready = false;
    failPending(QStringLiteral("MCP 已停止"));
}

// 写一行 JSON-RPC + 换行分帧；m_proc 已空（stop 后迟到调用）静默丢弃
void McpStdioClient::send(const QJsonObject &msg)
{
    if (!m_proc)
        return;
    m_proc->write(QJsonDocument(msg).toJson(QJsonDocument::Compact));
    m_proc->write("\n");
}

// 清空在途请求表并逐个以 {"error":{message:…}} 形状回调（回调方按约定判
// 失败）；错误/finished 双路径共用，拷贝后清空防回调内重入
void McpStdioClient::failPending(const QString &err)
{
    const auto pending = m_pending;
    m_pending.clear();
    // 应答统一带 error 字段，回调方按约定判失败
    for (const auto &cb : pending) {
        cb(QJsonObject{{"error", QJsonObject{{"message", err}}}});
    }
}

// 分帧后的单行 JSON-RPC 分发：无 id（通知，如 tools/list_changed）忽略；
// 按 id 查 pending 回调并移除（一次应答只触发一次）
void McpStdioClient::onStdoutLine(const QByteArray &line)
{
    const QJsonObject msg = QJsonDocument::fromJson(line).object();
    if (msg.isEmpty())
        return;
    if (!msg.contains(QStringLiteral("id")))
        return; // 通知（如 tools/list_changed）暂不处理
    const int id = msg.value(QStringLiteral("id")).toInt(-1);
    const auto it = m_pending.constFind(id);
    if (it == m_pending.cend())
        return;
    const auto cb = it.value();
    m_pending.remove(id);
    cb(msg.contains(QStringLiteral("result"))
           ? msg.value(QStringLiteral("result")).toObject()
           : QJsonObject{{"error", msg.value(QStringLiteral("error")).toObject()}});
}

void McpStdioClient::listTools(
    const std::function<void(bool, QJsonArray, QString)> &done)
{
    if (!m_ready) {
        done(false, {}, QStringLiteral("MCP[%1] 未就绪").arg(m_cfg.name));
        return;
    }
    const int id = m_nextId++;
    // 看门狗用下方 singleShot 实现（B-7：原此处有一个从未 start 的死代码
    // QTimer，仅在 pending 回调里 deleteLater——超时路径不经过回调，每个
    // 超时请求泄漏一个；直接删除，回调不再持有定时器）
    m_pending.insert(id, [this, done](const QJsonObject &result) {
        if (result.contains(QStringLiteral("error"))) {
            done(false, {},
                 result.value(QStringLiteral("error"))
                     .toObject()
                     .value(QStringLiteral("message"))
                     .toString());
            return;
        }
        done(true, result.value(QStringLiteral("tools")).toArray(), QString());
    });
    // 看门狗：超时走失败回调
    QTimer::singleShot(kRequestTimeoutMs, this, [this, id, done] {
        if (m_pending.contains(id)) {
            m_pending.remove(id);
            done(false, {}, QStringLiteral("MCP[%1] 请求超时").arg(m_cfg.name));
        }
    });
    send(QJsonObject{{"jsonrpc", QStringLiteral("2.0")},
                     {"id", id},
                     {"method", QStringLiteral("tools/list")},
                     {"params", QJsonObject{}}});
}

void McpStdioClient::callTool(const QString &tool, const QJsonObject &arguments,
                              const std::function<void(bool, QJsonArray, QString)> &done)
{
    if (!m_ready) {
        done(false, {}, QStringLiteral("MCP[%1] 未就绪").arg(m_cfg.name));
        return;
    }
    const int id = m_nextId++;
    m_pending.insert(id, [done](const QJsonObject &result) {
        if (result.contains(QStringLiteral("error"))) {
            done(false, {},
                 result.value(QStringLiteral("error"))
                     .toObject()
                     .value(QStringLiteral("message"))
                     .toString());
            return;
        }
        // tools/call 结果：{content:[{type:"text",text:…}], isError?:true}
        const bool isErr = result.value(QStringLiteral("isError")).toBool(false);
        done(!isErr, result.value(QStringLiteral("content")).toArray(),
             isErr ? QStringLiteral("工具报告执行失败") : QString());
    });
    QTimer::singleShot(kRequestTimeoutMs, this, [this, id, done] {
        if (m_pending.contains(id)) {
            m_pending.remove(id);
            done(false, {}, QStringLiteral("MCP[%1] 请求超时").arg(m_cfg.name));
        }
    });
    send(QJsonObject{{"jsonrpc", QStringLiteral("2.0")},
                     {"id", id},
                     {"method", QStringLiteral("tools/call")},
                     {"params", QJsonObject{{"name", tool},
                                            {"arguments", arguments}}}});
}

} // namespace poptools
