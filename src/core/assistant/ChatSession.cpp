#include "ChatSession.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

namespace poptools {

namespace {
constexpr int kMaxHistory = 40;        // system 之外的最大消息数（每会话）
constexpr int kSessionVersion = 2;     // T-050-P2：多会话格式（v1 装载时迁移）
constexpr int kLegacyVersion = 1;      // 旧单会话格式
constexpr int kSaveDebounceMs = 1000;  // 防抖落盘：工具密集回合最多 1s 一次全量写
constexpr int kTitleMaxChars = 24;     // 自动标题截断字数（AI 代决定案）

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
    // 从未 load 的实例（单测直 append 的用法/防御路径）也保证"恒有一个
    // 可用会话"不变量，append/size 直接可用
    ensureDefaultSession();
}

void ChatSession::load()
{
    m_sessions.clear();
    m_activeIndex = 0;
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) {
        ensureDefaultSession(); // 首次使用：空会话
        return;
    }
    const QByteArray raw = f.readAll();
    f.close();

    // R5-C1（对照既有 Store 损坏备份先例）：非 JSON/半截文件或版本不识别时，
    // 先把原件备份为 .bad 再空载——否则之后 GoodyModule::shutdown 的必发
    // save()（或任意一次 saveSoon 落盘）会用空/新历史覆盖原文件，聊天
    // 历史永久丢失且无恢复手段
    // T-050-P2：v1 单会话格式属"可识别的合法旧版"，无损迁移为单会话，
    // 不再走损坏路径；v2 为现行格式。
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    const QJsonObject root = doc.object();
    const int version = root.value(QStringLiteral("version")).toInt(-1);

    const auto corrupt = [&]() {
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
        m_sessions.clear();
        ensureDefaultSession();
    };

    if (!doc.isObject() || (version != kSessionVersion && version != kLegacyVersion)) {
        corrupt();
        return;
    }

    if (version == kLegacyVersion) {
        // ---- v1 迁移：单 messages 数组 → 单会话 ----
        const QJsonValue messagesVal = root.value(QStringLiteral("messages"));
        // P3-2 语义保留：version 合法但 messages 键缺失/非数组 → 损坏路径
        if (!messagesVal.isArray()) {
            corrupt();
            return;
        }
        SessionData s;
        s.id = QUuid::createUuid().toString(QUuid::Id128);
        s.updatedAtMs = QDateTime::currentMSecsSinceEpoch();
        const QJsonArray arr = messagesVal.toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("role")).toString() == QLatin1String("user")
                && s.title.isEmpty()) {
                s.title = autoSessionTitle(o.value(QStringLiteral("content")).toString());
                break;
            }
        }
        s.messages = filterLegalSequence(arr);
        m_sessions.append(s);
        m_activeIndex = 0;
        return;
    }

    // ---- v2 多会话 ----
    const QJsonValue sessionsVal = root.value(QStringLiteral("sessions"));
    if (!sessionsVal.isArray()) {
        corrupt(); // P3-2 同语义：version 合法但承载结构缺失
        return;
    }
    const QString activeId = root.value(QStringLiteral("active")).toString();
    int activeIdx = -1;
    int mostRecent = -1;
    qint64 mostRecentMs = -1;
    for (const QJsonValue &v : sessionsVal.toArray()) {
        const QJsonObject o = v.toObject();
        SessionData s;
        s.id = o.value(QStringLiteral("id")).toString();
        if (s.id.isEmpty())
            continue; // 无 id 条目：跳过（半损坏容错，非整文件损坏）
        s.title = o.value(QStringLiteral("title")).toString();
        s.updatedAtMs = qint64(o.value(QStringLiteral("updatedAt")).toDouble(0));
        // T-050-FIX2："messages 存在但非数组"与 v1 侧（:143-146）同语义并入
        // 损坏路径——原先 toArray() 对任意值都回空数组照收，下次 saveSoon
        // 就用空消息覆盖原件且无备份；键缺失（旧结构/新建空会话）仍允许
        // 置空——区分"缺失"与"存在但类型错"
        if (o.contains(QStringLiteral("messages"))
            && !o.value(QStringLiteral("messages")).isArray()) {
            corrupt();
            return;
        }
        s.messages = filterLegalSequence(o.value(QStringLiteral("messages")).toArray());
        if (s.id == activeId)
            activeIdx = m_sessions.size();
        if (s.updatedAtMs > mostRecentMs) {
            mostRecentMs = s.updatedAtMs;
            mostRecent = m_sessions.size();
        }
        m_sessions.append(s);
    }
    if (m_sessions.isEmpty()) {
        // v2 外壳合法但零会话：按空载处理（不备份——文件本身合法，只是空）
        ensureDefaultSession();
        return;
    }
    m_activeIndex = activeIdx >= 0 ? activeIdx
                                   : (mostRecent >= 0 ? mostRecent : 0);
}

void ChatSession::save()
{
    m_saveTimer->stop(); // 显式落盘即清挂起的防抖（退出 flush 用）
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    QJsonArray sessions;
    for (const SessionData &s : m_sessions) {
        sessions.append(QJsonObject{{"id", s.id},
                                    {"title", s.title},
                                    {"updatedAt", double(s.updatedAtMs)},
                                    {"messages", s.messages}});
    }
    const QJsonObject root{{"version", kSessionVersion},
                           {"active", activeSessionId()},
                           {"sessions", sessions}};
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
    QJsonArray &msgs = active().messages;
    if (!msgs.isEmpty() && msgs.first().toObject().value("role") == QLatin1String("system"))
        msgs.replace(0, sys);
    else
        msgs.prepend(sys);
}

void ChatSession::append(const QJsonObject &msg)
{
    // 入口护栏：无 role 的消息（失败路径回传的空对象）拒收——一旦入会话，
    // 后续每轮请求都会带上非法消息被 API 整包拒绝且自我延续
    if (!msg.contains(QStringLiteral("role"))
        || msg.value(QStringLiteral("role")).toString().isEmpty())
        return;
    SessionData &s = active();
    // T-050-P2：自动标题 = 会话首条用户消息截 ~24 字（不额外花 API 调用）
    if (s.title.isEmpty()
        && msg.value(QStringLiteral("role")).toString() == QLatin1String("user")) {
        s.title = autoSessionTitle(msg.value(QStringLiteral("content")).toString());
    }
    s.messages.append(msg);
    touch();
    trim();
    emit changed();
}

void ChatSession::appendToolResult(const QString &toolCallId, const QJsonObject &result)
{
    // OpenAI 协议要求 tool 消息的 content 为字符串：结果 JSON 序列化传递
    active().messages.append(QJsonObject{
        {"role", QStringLiteral("tool")},
        {"tool_call_id", toolCallId},
        {"content", QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))},
    });
    touch();
    trim();
    emit changed();
}

void ChatSession::clear()
{
    SessionData &s = active();
    s.messages = QJsonArray{};
    s.title.clear(); // 清空会话连标题一并复位：下一条用户消息重新起题
    touch();
    emit changed();
}

// ---- T-050-P2：多会话管理 ----

QString ChatSession::activeSessionId() const
{
    return m_sessions.isEmpty() ? QString() : m_sessions[m_activeIndex].id;
}

QList<ChatSession::SessionSummary> ChatSession::sessionSummaries() const
{
    QList<SessionSummary> out;
    out.reserve(m_sessions.size());
    for (const SessionData &s : m_sessions) {
        // T-050-FIX2：侧栏"N 条"计数排除 system（人格提示词每会话恒占
        // 一条，计入会误导用户对会话长度的判断）
        int messageCount = 0;
        for (const QJsonValue &v : s.messages) {
            if (v.toObject().value(QStringLiteral("role")).toString()
                != QLatin1String("system"))
                ++messageCount;
        }
        out.append(SessionSummary{s.id, s.title, s.updatedAtMs, messageCount});
    }
    std::sort(out.begin(), out.end(),
              [](const SessionSummary &a, const SessionSummary &b) {
                  if (a.updatedAtMs != b.updatedAtMs)
                      return a.updatedAtMs > b.updatedAtMs; // 最近活跃在前
                  return a.id < b.id; // 同刻（新建连发）按 id 稳定排序
              });
    return out;
}

QString ChatSession::createSession()
{
    SessionData s;
    s.id = QUuid::createUuid().toString(QUuid::Id128);
    s.updatedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_sessions.append(s);
    m_activeIndex = m_sessions.size() - 1;
    emit changed();
    return s.id;
}

bool ChatSession::switchSession(const QString &id)
{
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i].id == id) {
            if (i != m_activeIndex) {
                m_activeIndex = i;
                emit changed();
            }
            return true;
        }
    }
    return false;
}

bool ChatSession::deleteSession(const QString &id)
{
    int idx = -1;
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i].id == id) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return false;
    const bool wasActive = idx == m_activeIndex;
    m_sessions.removeAt(idx);
    if (m_sessions.isEmpty()) {
        ensureDefaultSession(); // 最后一格也删：新建空会话顶上（恒有会话不变量）
        emit changed();
        return true;
    }
    if (wasActive) {
        // 删除当前会话 → 切最近活跃的余下会话
        m_activeIndex = 0;
        qint64 best = -1;
        for (int i = 0; i < m_sessions.size(); ++i) {
            if (m_sessions[i].updatedAtMs > best) {
                best = m_sessions[i].updatedAtMs;
                m_activeIndex = i;
            }
        }
    } else if (idx < m_activeIndex) {
        --m_activeIndex; // 保持 active 指向不变的那个会话
    }
    emit changed();
    return true;
}

// ---- T-050-P2：消息操作域 API ----

void ChatSession::truncateFrom(int index, bool includeIndex)
{
    QJsonArray &msgs = active().messages;
    const int start = includeIndex ? index : index + 1;
    if (start < 0 || start >= msgs.size())
        return; // 越界：不动数据（调用方以消息数组快照取的 index，防御）
    while (msgs.size() > start)
        msgs.removeAt(msgs.size() - 1);
    // 自愈保险：截断点理应在 user 消息上（编辑重发入口），此处统一过一遍
    // 序列合法性过滤，杜绝任何入口留下的孤儿 tool 序列
    msgs = filterLegalSequence(msgs);
    touch();
    emit changed();
}

bool ChatSession::removeMessageAt(int index)
{
    QJsonArray &msgs = active().messages;
    if (index < 0 || index >= msgs.size())
        return false;
    const QJsonObject head = msgs.at(index).toObject();
    int end = index + 1;
    // assistant(tool_calls)：向后吃掉 id 配对的 tool 回应整组（防孤儿序列）；
    // 其余角色只删自身
    if (head.value(QStringLiteral("role")).toString() == QLatin1String("assistant")
        && head.contains(QLatin1String("tool_calls"))) {
        QSet<QString> waiting;
        const QJsonArray calls = head.value(QLatin1String("tool_calls")).toArray();
        for (const QJsonValue &c : calls) {
            const QString cid = c.toObject().value(QStringLiteral("id")).toString();
            if (!cid.isEmpty())
                waiting.insert(cid);
        }
        while (end < msgs.size() && !waiting.isEmpty()) {
            const QJsonObject t = msgs.at(end).toObject();
            if (t.value(QStringLiteral("role")).toString() != QLatin1String("tool"))
                break;
            const QString tid = t.value(QStringLiteral("tool_call_id")).toString();
            if (!waiting.contains(tid))
                break;
            waiting.remove(tid);
            ++end;
        }
    }
    for (int i = end - 1; i >= index; --i)
        msgs.removeAt(i);
    msgs = filterLegalSequence(msgs); // 自愈保险（同 truncateFrom）
    touch();
    emit changed();
    return true;
}

QString ChatSession::autoSessionTitle(const QString &firstUserText)
{
    QString t = firstUserText;
    t.replace(QLatin1Char('\r'), QLatin1Char(' '));
    t.replace(QLatin1Char('\n'), QLatin1Char(' '));
    t = t.simplified().trimmed(); // 连续空白折叠为单空格
    if (t.size() > kTitleMaxChars)
        return t.left(kTitleMaxChars) + QStringLiteral("…");
    return t;
}

// ---- 内部 ----

void ChatSession::ensureDefaultSession()
{
    if (!m_sessions.isEmpty())
        return;
    SessionData s;
    s.id = QUuid::createUuid().toString(QUuid::Id128);
    s.updatedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_sessions.append(s);
    m_activeIndex = 0;
}

void ChatSession::touch()
{
    active().updatedAtMs = QDateTime::currentMSecsSinceEpoch();
}

void ChatSession::trim()
{
    // 保留 system（若在首位）+ 最近 kMaxHistory 条。
    // 按"完整配对组"淘汰：assistant(tool_calls) 与其后连续 tool 消息作为
    // 整体删除——逐条盲删会拆散配对（留下孤儿 tool_calls 或孤儿 tool 消息），
    // 之后每轮请求都带着非法序列被 API 整包拒绝
    QJsonArray &msgs = active().messages;
    const bool hasSystem =
        !msgs.isEmpty()
        && msgs.first().toObject().value("role") == QLatin1String("system");
    const int maxTotal = kMaxHistory + (hasSystem ? 1 : 0);
    while (msgs.size() > maxTotal) {
        // 从 system 之后开始丢最旧；永不动 system
        const int idx = hasSystem ? 1 : 0;
        int end = idx + 1;
        const QJsonObject head = msgs.at(idx).toObject();
        if (head.value(QStringLiteral("role")) == QLatin1String("assistant")
            && head.contains(QLatin1String("tool_calls"))) {
            while (end < msgs.size()
                   && msgs.at(end).toObject().value(QStringLiteral("role"))
                       == QLatin1String("tool"))
                ++end;
        }
        for (int i = end - 1; i >= idx; --i)
            msgs.removeAt(i);
    }
}

} // namespace poptools
