#include "McpStdioClient.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTimer>

namespace poptools {

namespace {
constexpr int kRequestTimeoutMs = 30 * 1000;
constexpr const char *kProtocolVersion = "2024-11-05";
} // namespace

QList<McpStdioClient::ServerConfig> McpStdioClient::loadConfig(const QString &goodyDir)
{
    QList<ServerConfig> out;
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
        if (!c.name.isEmpty() && !c.command.isEmpty())
            out << c; // 条目不全的跳过（配置容错）
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
    // 子进程死亡：清空在途请求（错误语义），ready 回退
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_ready) {
            m_ready = false;
            emit readyChanged();
        }
        failPending(QStringLiteral("MCP 进程异常退出"));
    });
    connect(m_proc, &QProcess::finished, this, [this] { failPending(QStringLiteral("MCP 进程已退出")); });
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
        Q_UNUSED(result);
        // initialized 是通知（无 id，不应答）
        QJsonObject note{{"jsonrpc", QStringLiteral("2.0")},
                         {"method", QStringLiteral("notifications/initialized")}};
        send(note);
        m_ready = true;
        emit readyChanged();
    });
    send(req);
    // 握手看门狗：30s 无应答判失败（pending 清空即永久失败）
    QTimer::singleShot(kRequestTimeoutMs, this, [this, id] {
        if (m_pending.contains(id)) {
            m_pending.remove(id);
            qWarning("MCP[%s]: initialize 握手超时", qPrintable(m_cfg.name));
        }
    });
}

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

void McpStdioClient::send(const QJsonObject &msg)
{
    if (!m_proc)
        return;
    m_proc->write(QJsonDocument(msg).toJson(QJsonDocument::Compact));
    m_proc->write("\n");
}

void McpStdioClient::failPending(const QString &err)
{
    const auto pending = m_pending;
    m_pending.clear();
    // 应答统一带 error 字段，回调方按约定判失败
    for (const auto &cb : pending) {
        cb(QJsonObject{{"error", QJsonObject{{"message", err}}}});
    }
}

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
