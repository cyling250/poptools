#include "ChatSession.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>

namespace poptools {

namespace {
constexpr int kMaxHistory = 40;        // system 之外的最大消息数
constexpr int kSessionVersion = 1;
constexpr int kSaveDebounceMs = 1000;  // 防抖落盘：工具密集回合最多 1s 一次全量写
} // namespace

ChatSession::ChatSession(const QString &storePath, QObject *parent)
    : QObject(parent)
    , m_path(storePath)
{
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(kSaveDebounceMs);
    connect(m_saveTimer, &QTimer::timeout, this, &ChatSession::save);
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
    m_saveTimer->stop(); // 显式落盘即清挂起的防抖（退出 flush 用）
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    const QJsonObject root{{"version", kSessionVersion}, {"messages", m_messages}};
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.commit();
}

void ChatSession::saveSoon()
{
    // 脏标记防抖（红线1：主线程磁盘 IO 节流）：每个工具结果都全量写盘
    // 改为最多 kSaveDebounceMs 一次；退出路径由 GoodyModule::shutdown 的
    // 显式 save() 兜底 flush
    if (!m_saveTimer->isActive())
        m_saveTimer->start();
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
    // 保留 system（若在首位）+ 最近 kMaxHistory 条。
    // 按"完整配对组"淘汰：assistant(tool_calls) 与其后连续 tool 消息作为
    // 整体删除——逐条盲删会拆散配对（留下孤儿 tool_calls 或孤儿 tool 消息），
    // 之后每轮请求都带着非法序列被 API 整包拒绝
    const bool hasSystem =
        !m_messages.isEmpty()
        && m_messages.first().toObject().value("role") == QLatin1String("system");
    const int maxTotal = kMaxHistory + (hasSystem ? 1 : 0);
    while (m_messages.size() > maxTotal) {
        // 从 system 之后开始丢最旧；永不动 system
        const int idx = hasSystem ? 1 : 0;
        int end = idx + 1;
        const QJsonObject head = m_messages.at(idx).toObject();
        if (head.value(QStringLiteral("role")) == QLatin1String("assistant")
            && head.contains(QLatin1String("tool_calls"))) {
            while (end < m_messages.size()
                   && m_messages.at(end).toObject().value(QStringLiteral("role"))
                       == QLatin1String("tool"))
                ++end;
        }
        for (int i = end - 1; i >= idx; --i)
            m_messages.removeAt(i);
    }
}

} // namespace poptools
