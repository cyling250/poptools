#include "ChatSession.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace poptools {

namespace {
constexpr int kMaxHistory = 40;   // system 之外的最大消息数
constexpr int kSessionVersion = 1;
} // namespace

ChatSession::ChatSession(const QString &storePath, QObject *parent)
    : QObject(parent)
    , m_path(storePath)
{
}

void ChatSession::load()
{
    m_messages = QJsonArray{};
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return; // 首次使用：空会话
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt() != kSessionVersion)
        return; // 版本不识别：按空会话处理（不尝试兼容旧格式）
    // 装载时过滤非法消息（无 role 的空对象会让整包请求被 API 422 拒掉，
    // 见 GoodyModule 失败路径的历史教训）
    const QJsonArray arr = root.value(QStringLiteral("messages")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("role")) && !o.value(QStringLiteral("role")).toString().isEmpty())
            m_messages.append(o);
    }
}

void ChatSession::save()
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    const QJsonObject root{{"version", kSessionVersion}, {"messages", m_messages}};
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.commit();
}

void ChatSession::setSystemPrompt(const QString &prompt)
{
    QJsonObject sys{{"role", QStringLiteral("system")}, {"content", prompt}};
    if (!m_messages.isEmpty() && m_messages.first().toObject().value("role") == QLatin1String("system"))
        m_messages.replace(0, sys);
    else
        m_messages.prepend(sys);
}

void ChatSession::append(const QJsonObject &msg)
{
    // 入口护栏：无 role 的消息（失败路径回传的空对象）拒收——一旦入会话，
    // 后续每轮请求都会带上非法消息被 API 整包拒绝且自我延续
    if (!msg.contains(QStringLiteral("role"))
        || msg.value(QStringLiteral("role")).toString().isEmpty())
        return;
    m_messages.append(msg);
    trim();
}

void ChatSession::appendToolResult(const QString &toolCallId, const QJsonObject &result)
{
    // OpenAI 协议要求 tool 消息的 content 为字符串：结果 JSON 序列化传递
    m_messages.append(QJsonObject{
        {"role", QStringLiteral("tool")},
        {"tool_call_id", toolCallId},
        {"content", QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))},
    });
    trim();
}

void ChatSession::clear()
{
    m_messages = QJsonArray{};
}

void ChatSession::trim()
{
    // 保留 system（若在首位）+ 最近 kMaxHistory 条
    const bool hasSystem =
        !m_messages.isEmpty()
        && m_messages.first().toObject().value("role") == QLatin1String("system");
    const int maxTotal = kMaxHistory + (hasSystem ? 1 : 0);
    while (m_messages.size() > maxTotal) {
        // 从 system 之后开始丢最旧；永不动 system
        m_messages.removeAt(hasSystem ? 1 : 0);
    }
}

} // namespace poptools
