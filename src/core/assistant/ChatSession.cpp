#include "ChatSession.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

namespace poptools {

namespace {
constexpr int kMaxHistory = 40;        // system 之外的最大消息数
constexpr int kSessionVersion = 1;
constexpr int kSaveDebounceMs = 1000;  // 防抖落盘：工具密集回合最多 1s 一次全量写

// R5-C2：装载期序列合法性过滤（与 trim 的"配对组"淘汰同思路的最小过滤）。
// 丢弃：① 无 role 的空对象（既有过滤）② 孤儿 tool（缺 tool_call_id，或
// 前面没有待回应的 assistant tool_calls）③ tool_calls 后缺 tool 回应的
// 断裂 assistant 项（连同其不完整的 tool 回应整组丢弃）。手工编辑/半损坏
// 的会话文件若把非法序列带进下一轮请求，网关 400 且 trim 无法自愈。
QJsonArray filterLegalSequence(const QJsonArray &arr)
{
    QJsonArray out;
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject o = arr.at(i).toObject();
        const QString role = o.value(QStringLiteral("role")).toString();
        if (role.isEmpty())
            continue;
        // R5-C2 复审修正（P2-1）：role 白名单——仅放行 system/user/assistant/
        // tool；旧协议残留的 role:"function" 等直接丢弃，防带入下一轮请求
        // 被网关 400 整包拒绝
        if (role != QLatin1String("system") && role != QLatin1String("user")
            && role != QLatin1String("assistant") && role != QLatin1String("tool"))
            continue;
        if (role != QLatin1String("assistant") || !o.contains(QLatin1String("tool_calls"))) {
            if (role == QLatin1String("tool"))
                continue; // 孤儿 tool：前面没有待回应的 assistant tool_calls
            out.append(o);
            continue;
        }
        // assistant + tool_calls：收集 id，向后吃掉连续且 id 匹配的 tool 回应
        QSet<QString> waiting;
        const QJsonArray calls = o.value(QLatin1String("tool_calls")).toArray();
        for (const QJsonValue &c : calls) {
            const QString id = c.toObject().value(QStringLiteral("id")).toString();
            if (!id.isEmpty())
                waiting.insert(id);
        }
        if (waiting.isEmpty())
            continue; // tool_calls 为空/无有效 id：非法，整条丢弃
        int end = i + 1;
        while (end < arr.size() && !waiting.isEmpty()) {
            const QJsonObject t = arr.at(end).toObject();
            // R5-C2 复审修正（P3-3）：断组判定——有意保守：宁可整组丢弃也不过网关 400
            if (t.value(QStringLiteral("role")).toString() != QLatin1String("tool"))
                break;
            const QString tid = t.value(QStringLiteral("tool_call_id")).toString();
            if (!waiting.contains(tid))
                break; // id 不匹配：连续回应中断
            waiting.remove(tid);
            ++end;
        }
        if (!waiting.isEmpty())
            continue; // 断裂配对：tool 回应不全，assistant 与其后 tool 整组丢弃
        out.append(o);
        for (int j = i + 1; j < end; ++j)
            out.append(arr.at(j).toObject());
        i = end - 1; // 已消费的 tool 回应整体跳过
    }
    return out;
}
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
    const QByteArray raw = f.readAll();
    f.close();

    // R5-C1（对照 BoxStore P2-13 先例）：非 JSON/半截文件或版本不识别时，
    // 先把原件备份为 .bad 再空载——否则之后 GoodyModule::shutdown 的必发
    // save()（或任意一次 saveSoon 落盘）会用空/新历史覆盖原文件，聊天
    // 历史永久丢失且无恢复手段
    // R5-C2 复审修正（P3-2）：version 合法但 "messages" 键缺失或非数组时，
    // 原先静默空载无备份，之后同样会被 save() 用空历史覆盖——并入损坏
    // 处理路径（同样 .bad 备份 + 空载）
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    const QJsonObject root = doc.object();
    const QJsonValue messagesVal = root.value(QStringLiteral("messages"));
    if (!doc.isObject()
        || root.value(QStringLiteral("version")).toInt() != kSessionVersion
        || !messagesVal.isArray()) {
        const QString badPath = m_path + QStringLiteral(".bad");
        // R5-C2 复审修正（P3-1）：.bad 只保留两代——已有备份先挪为 .bad.old
        //（覆盖上一代 .bad.old）再写新 .bad，备份磁盘占用有界
        const QString badOldPath = m_path + QStringLiteral(".bad.old");
        if (QFile::exists(badPath)) {
            QFile::remove(badOldPath);
            QFile::rename(badPath, badOldPath);
        }
        if (QFile::copy(m_path, badPath))
            qWarning("会话数据损坏或版本不识别，原件已备份为 %s", qUtf8Printable(badPath));
        else
            qWarning("会话数据损坏，且备份失败：%s", qUtf8Printable(m_path));
        return;
    }
    // 装载时过滤非法消息（无 role 的空对象会让整包请求被 API 422 拒掉，
    // 见 GoodyModule 失败路径的历史教训；其余按 filterLegalSequence 的
    // 配对组校验过滤）
    m_messages = filterLegalSequence(messagesVal.toArray());
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
