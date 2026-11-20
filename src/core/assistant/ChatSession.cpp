#include "ChatSession.h"

#include "core/monitor/MainThreadProbe.h" // T-070b 落盘打点

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
constexpr int kMaxHistory = 40;        // 投影消息上限（每会话，超限按回合块淘汰）
constexpr int kSessionVersion = 3;     // T-070：事件日志格式（v1/v2 装载时迁移）
constexpr int kLegacyVersion = 1;      // 旧单会话格式
constexpr int kSaveDebounceMs = 1000;  // 防抖落盘：工具密集回合最多 1s 一次全量写
constexpr int kTitleMaxChars = 24;     // 自动标题截断字数（AI 代决定案）

// ---- 事件 type 常量（v3 事件日志词汇表，dsh known-event-types 的 Qt 最小集） ----
constexpr const char *kEvTurnStart = "turn/start";
constexpr const char *kEvTurnEnd = "turn/end";
constexpr const char *kEvSystem = "system/message";
constexpr const char *kEvUser = "user/message";
constexpr const char *kEvAssistant = "assistant/message";
constexpr const char *kEvToolResult = "tool/result";
constexpr const char *kEvApprovalAsked = "approval/asked";
constexpr const char *kEvApprovalDecided = "approval/decided";

QString evType(const QJsonObject &e)
{
    return e.value(QStringLiteral("type")).toString();
}

// 投影首条是否 system 消息（侧栏计数/trim 口径共用）
bool hasSystemIn(const QJsonArray &msgs)
{
    return !msgs.isEmpty()
        && msgs.at(0).toObject().value(QStringLiteral("role")).toString()
               == QLatin1String("system");
}

// 区间内投影消息数（user/assistant/tool/result 各计 1；审计/边界事件不计）。
// trim 估算回合块淘汰量用，不触碰投影缓存
int countProjectedEvents(const QJsonArray &events, int begin, int end)
{
    int n = 0;
    for (int i = begin; i < end; ++i) {
        const QString t = evType(events.at(i).toObject());
        if (t == QLatin1String(kEvUser) || t == QLatin1String(kEvAssistant)
            || t == QLatin1String(kEvToolResult))
            ++n;
    }
    return n;
}

// v2 messages 数组的序列合法性过滤（迁移入口用，原 R5-C2 逻辑原样保留）。
// 丢弃：① 无 role 的空对象 ② role 不在白名单 ③ 孤儿 tool（缺配对）
// ④ tool_calls 后缺 tool 回应的断裂 assistant 项（整组丢弃）
QJsonArray filterLegalSequence(const QJsonArray &arr)
{
    QJsonArray out;
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject o = arr.at(i).toObject();
        const QString role = o.value(QStringLiteral("role")).toString();
        if (role.isEmpty())
            continue;
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

// v2 单条 message → v3 事件序列（迁移用）。turn 边界：每条 user 消息是
// 回合块起点（迁移历史无真实 turn 语义，回合块按 user 起点划定即可）
void migrateMessage(const QJsonObject &m, QJsonArray &events)
{
    const QString role = m.value(QStringLiteral("role")).toString();
    if (role == QLatin1String("system")) {
        events.append(QJsonObject{{"type", kEvSystem},
                                  {"text", m.value(QStringLiteral("content")).toString()}});
        return;
    }
    if (role == QLatin1String("user")) {
        events.append(QJsonObject{{"type", kEvUser},
                                  {"text", m.value(QStringLiteral("content")).toString()}});
        return;
    }
    if (role == QLatin1String("tool")) {
        events.append(QJsonObject{{"type", kEvToolResult},
                                  {"callId", m.value(QStringLiteral("tool_call_id")).toString()},
                                  {"content", m.value(QStringLiteral("content")).toString()},
                                  {"isError", false}});
        return;
    }
    if (role == QLatin1String("assistant")) {
        QJsonArray blocks;
        const QString content = m.value(QStringLiteral("content")).toString();
        if (!content.isEmpty())
            blocks.append(asst_blocks::textBlock(content));
        const QString reasoning = m.value(QStringLiteral("reasoning")).toString();
        if (!reasoning.isEmpty())
            blocks.append(asst_blocks::reasoningBlock(reasoning));
        const QJsonArray calls = m.value(QStringLiteral("tool_calls")).toArray();
        for (const QJsonValue &c : calls) {
            const QJsonObject fn = c.toObject().value(QStringLiteral("function")).toObject();
            blocks.append(asst_blocks::toolCallBlock(
                c.toObject().value(QStringLiteral("id")).toString(),
                fn.value(QStringLiteral("name")).toString(),
                fn.value(QStringLiteral("arguments")).toString()));
        }
        events.append(QJsonObject{{"type", kEvAssistant},
                                  {"blocks", blocks},
                                  {"finish", calls.isEmpty()
                                                 ? QStringLiteral("stop")
                                                 : QStringLiteral("tool_calls")}});
    }
}
} // namespace

ChatSession::ChatSession(const QString &storePath, QObject *parent)
    : QObject(parent)
    , m_path(storePath)
{
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(kSaveDebounceMs);
    connect(m_saveTimer, &QTimer::timeout, this, &ChatSession::saveAsync);
    m_ioPool.setMaxThreadCount(1); // 落盘写串行化：不重叠、后到后提交
    // 从未 load 的实例（单测直 append 的用法/防御路径）也保证"恒有一个
    // 可用会话"不变量，append/size 直接可用
    ensureDefaultSession();
}

ChatSession::~ChatSession()
{
    // 防抖写走 IO 池：析构等待在途写完成（lambda 只捕快照值不捕 this，
    // 对象先亡也不悬垂；这里等待只为"退出即落盘"的完整性）
    m_ioPool.waitForDone();
}

void ChatSession::load()
{
    m_sessions.clear();
    m_activeIndex = 0;
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) {
        ensureDefaultSession(); // 首次使用：空会话
        rederive();
        return;
    }
    const QByteArray raw = f.readAll();
    f.close();

    // R5-C1（对照既有 Store 损坏备份先例）：非 JSON/半截文件或版本不识别时，
    // 先把原件备份为 .bad 再空载——否则之后必发的 save() 会用空/新历史覆盖
    // 原文件，聊天历史永久丢失且无恢复手段。v1/v2 属"可识别的合法旧版"，
    // 无损迁移，不走损坏路径
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    const QJsonObject root = doc.object();
    const int version = root.value(QStringLiteral("version")).toInt(-1);

    const auto corrupt = [&]() {
        const QString badPath = m_path + QStringLiteral(".bad");
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
        rederive();
    };

    if (!doc.isObject()
        && version != kLegacyVersion /* v1 空对象容错：按损坏处理 */) {
        corrupt();
        return;
    }
    if (version != kSessionVersion && version != kLegacyVersion
        && version != 2) {
        corrupt();
        return;
    }

    if (version == kLegacyVersion || version == 2) {
        // ---- v1/v2 迁移：messages 数组 → 事件序列 ----
        QJsonArray msgs;
        if (version == kLegacyVersion) {
            const QJsonValue messagesVal = root.value(QStringLiteral("messages"));
            if (!messagesVal.isArray()) {
                corrupt();
                return;
            }
            msgs = messagesVal.toArray();
        } else {
            const QJsonValue sessionsVal = root.value(QStringLiteral("sessions"));
            if (!sessionsVal.isArray()) {
                corrupt();
                return;
            }
            const QString activeId = root.value(QStringLiteral("active")).toString();
            for (const QJsonValue &v : sessionsVal.toArray()) {
                const QJsonObject o = v.toObject();
                SessionData s;
                s.id = o.value(QStringLiteral("id")).toString();
                if (s.id.isEmpty())
                    continue;
                s.title = o.value(QStringLiteral("title")).toString();
                s.updatedAtMs = qint64(o.value(QStringLiteral("updatedAt")).toDouble(0));
                if (o.contains(QStringLiteral("messages"))
                    && !o.value(QStringLiteral("messages")).isArray()) {
                    corrupt();
                    return;
                }
                s.events = QJsonArray{};
                const QJsonArray arr = o.value(QStringLiteral("messages")).toArray();
                // 临时容器不直接 range-for（Qt 容器悬垂陷阱）：先落具名变量
                const QJsonArray legal = filterLegalSequence(arr);
                for (const QJsonValue &m : legal)
                    migrateMessage(m.toObject(), s.events);
                if (s.id == activeId)
                    m_activeIndex = m_sessions.size();
                m_sessions.append(s);
            }
            if (m_sessions.isEmpty())
                ensureDefaultSession();
            for (SessionData &s : m_sessions)
                s.events = std::move(s.events); // 语义标注（无实际移动必要）
            for (int i = 0; i < m_sessions.size(); ++i) {
                if (i == m_activeIndex) {
                    // 重投影当前会话；其余会话按需在切换时投影
                }
            }
            m_derived = QJsonArray{};
            m_eventStartByMsg.clear();
            // 当前会话投影
            if (!m_sessions.isEmpty()) {
                deriveEvents(m_sessions[m_activeIndex].events, m_derived,
                             m_eventStartByMsg);
            }
            // 标题缺失的自愈沿用 v2 装载（title 已在文件里，不重建）
            return;
        }
        // v1：单会话迁移
        SessionData s;
        s.id = QUuid::createUuid().toString(QUuid::Id128);
        s.updatedAtMs = QDateTime::currentMSecsSinceEpoch();
        for (const QJsonValue &v : msgs) {
            const QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("role")).toString() == QLatin1String("user")
                && s.title.isEmpty()) {
                s.title = autoSessionTitle(o.value(QStringLiteral("content")).toString());
                break;
            }
        }
        const QJsonArray legal = filterLegalSequence(msgs);
        for (const QJsonValue &m : legal)
            migrateMessage(m.toObject(), s.events);
        m_sessions.append(s);
        m_activeIndex = 0;
        rederive();
        return;
    }

    // ---- v3 事件日志 ----
    const QJsonValue sessionsVal = root.value(QStringLiteral("sessions"));
    if (!sessionsVal.isArray()) {
        corrupt();
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
        if (o.contains(QStringLiteral("events"))
            && !o.value(QStringLiteral("events")).isArray()) {
            corrupt();
            return;
        }
        s.events = o.value(QStringLiteral("events")).toArray();
        if (s.id == activeId)
            activeIdx = m_sessions.size();
        if (s.updatedAtMs > mostRecentMs) {
            mostRecentMs = s.updatedAtMs;
            mostRecent = m_sessions.size();
        }
        m_sessions.append(s);
    }
    if (m_sessions.isEmpty()) {
        ensureDefaultSession();
        rederive();
        return;
    }
    m_activeIndex = activeIdx >= 0 ? activeIdx
                                   : (mostRecent >= 0 ? mostRecent : 0);
    rederive();
}

void ChatSession::save()
{
    // T-070b：全量写盘打点（会话事件日志随使用增长，QSaveFile 全量写是
    // 后台停摆候选——探针 mark 归因）
    if (auto *p = MainThreadProbe::instance())
        p->mark("chat_save_begin");
    m_saveTimer->stop(); // 显式落盘即清挂起的防抖（退出 flush 用）
    m_ioPool.waitForDone(); // 排空在途/排队的异步快照，保证本同步写最后提交
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("会话落盘失败（打开文件）：%s：%s", qUtf8Printable(m_path),
                 qUtf8Printable(f.errorString()));
        return;
    }
    QJsonArray sessions;
    for (const SessionData &s : m_sessions) {
        sessions.append(QJsonObject{{"id", s.id},
                                    {"title", s.title},
                                    // 毫秒时间戳走 double：JSON number 53 位
                                    // 精度内 1.7e12 无损（迁移口径不变）
                                    {"updatedAt", double(s.updatedAtMs)},
                                    {"events", s.events}});
    }
    const QJsonObject root{{"version", kSessionVersion},
                           {"active", activeSessionId()},
                           {"sessions", sessions}};
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!f.commit())
        qWarning("会话落盘失败（提交）：%s", qUtf8Printable(m_path));
    if (auto *p = MainThreadProbe::instance())
        p->mark("chat_save_end");
}

// 防抖到期（T-071）：序列化 + 写盘挪 IO 池——工具密集回合旧路径每秒一次
// 全会话全量序列化+写盘在 GUI 线程（红线①磁盘 IO 禁入主线程）。
// 快照 = 浅拷贝（QJsonArray COW 共享）：池内序列化读共享数据，主线程此后
// 的 append 触发 detach 各改各的，互不干扰。lambda 不捕 this——对象析构
// 后在途写仍安全完成
void ChatSession::saveAsync()
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QJsonArray sessions;
    for (const SessionData &s : m_sessions) {
        sessions.append(QJsonObject{{"id", s.id},
                                    {"title", s.title},
                                    {"updatedAt", double(s.updatedAtMs)},
                                    {"events", s.events}});
    }
    const QJsonObject root{{"version", kSessionVersion},
                           {"active", activeSessionId()},
                           {"sessions", sessions}};
    const QString path = m_path;
    m_ioPool.start([path, root] {
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            qWarning("会话落盘失败（打开文件）：%s：%s", qUtf8Printable(path),
                     qUtf8Printable(f.errorString()));
            return;
        }
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        if (!f.commit())
            qWarning("会话落盘失败（提交）：%s", qUtf8Printable(path));
    });
}

void ChatSession::saveSoon()
{
    if (!m_saveTimer->isActive())
        m_saveTimer->start();
}

// ---- 事件 API ----

void ChatSession::appendEvent(const QJsonObject &event)
{
    SessionData &s = active();
    const int ei = s.events.size();
    s.events.append(event);
    touch();
    projectAppended(event, ei); // 增量投影：只消费新事件（旧路径每事件全量
                                // 重投影，回合期累计 O(n²)）
    trim();
    active().messageCount =
        m_derived.size() - (hasSystemIn(m_derived) ? 1 : 0); // 侧栏口径同步
    emit changed();
}

void ChatSession::turnStart()
{
    appendEvent(QJsonObject{{"type", kEvTurnStart}});
}

void ChatSession::turnEnd(const QString &reason)
{
    appendEvent(QJsonObject{{"type", kEvTurnEnd}, {"reason", reason}});
}

void ChatSession::setSystemPrompt(const QString &prompt)
{
    // 内容与日志末条 system 相同则不追加（保住请求前缀，dsh 变化才替换语义）
    for (int i = active().events.size() - 1; i >= 0; --i) {
        const QJsonObject e = active().events.at(i).toObject();
        if (evType(e) == QLatin1String(kEvSystem)) {
            if (e.value(QStringLiteral("text")).toString() == prompt)
                return;
            break;
        }
        if (evType(e) == QLatin1String(kEvUser) || evType(e) == QLatin1String(kEvAssistant))
            break; // 只看最近一段（新的 system 必然在旧 system 之后、最新回合前）
    }
    appendEvent(QJsonObject{{"type", kEvSystem}, {"text", prompt}});
}

void ChatSession::appendUserMessage(const QString &text)
{
    SessionData &s = active();
    // 自动标题 = 会话首条用户消息截 ~24 字（不额外花 API 调用）
    if (s.title.isEmpty())
        s.title = autoSessionTitle(text);
    appendEvent(QJsonObject{{"type", kEvUser}, {"text", text}});
}

void ChatSession::appendAssistantBlocks(const QJsonArray &blocks, const QString &finish)
{
    appendEvent(QJsonObject{{"type", kEvAssistant},
                            {"blocks", blocks},
                            {"finish", finish}});
}

void ChatSession::appendToolResult(const QString &callId, const QString &content,
                                   bool isError)
{
    appendEvent(QJsonObject{{"type", kEvToolResult},
                            {"callId", callId},
                            {"content", content},
                            {"isError", isError}});
}

void ChatSession::appendApprovalAsked(const QString &callId, const QString &name)
{
    appendEvent(QJsonObject{{"type", kEvApprovalAsked},
                            {"callId", callId},
                            {"name", name}});
}

void ChatSession::appendApprovalDecided(const QString &callId, const QString &decision)
{
    appendEvent(QJsonObject{{"type", kEvApprovalDecided},
                            {"callId", callId},
                            {"decision", decision}});
}

void ChatSession::clear()
{
    SessionData &s = active();
    s.events = QJsonArray{};
    s.title.clear(); // 清空会话连标题一并复位：下一条用户消息重新起题
    touch();
    rederive();
    emit changed();
}

// ---- 投影 ----

// 单事件投影（system 事件除外——"取最后一条"需整流语境，走 deriveEvents
// 全量或 projectAppended 的替换逻辑）。返回是否投影（审计/边界事件 false）
static bool deriveOneEvent(const QJsonObject &e, int ei, QJsonArray &outMessages,
                           QList<int> &outEventStart)
{
    const QString t = evType(e);
    if (t == QLatin1String(kEvUser)) {
        outMessages.append(QJsonObject{{"role", QStringLiteral("user")},
                                       {"content", e.value(QStringLiteral("text")).toString()}});
        outEventStart.append(ei);
        return true;
    }
    if (t == QLatin1String(kEvAssistant)) {
        QString text, reasoning;
        QJsonArray toolCalls;
        const QJsonArray blocksArr = e.value(QStringLiteral("blocks")).toArray();
        for (const QJsonValue &b : blocksArr) {
            const QJsonObject blk = b.toObject();
            const QString bt = blk.value(QStringLiteral("t")).toString();
            if (bt == QLatin1String(asst_blocks::kText))
                text += blk.value(QStringLiteral("text")).toString();
            else if (bt == QLatin1String(asst_blocks::kReasoning))
                reasoning += blk.value(QStringLiteral("text")).toString();
            else if (bt == QLatin1String(asst_blocks::kToolCall))
                toolCalls.append(QJsonObject{
                    {"id", blk.value(QStringLiteral("id")).toString()},
                    {"type", QStringLiteral("function")},
                    {"function", QJsonObject{
                                     {"name", blk.value(QStringLiteral("name")).toString()},
                                     {"arguments", blk.value(QStringLiteral("args")).toString()}}}});
        }
        QJsonObject m{{"role", QStringLiteral("assistant")},
                      {"content", text}};
        if (!reasoning.isEmpty())
            m.insert(QStringLiteral("reasoning"), reasoning); // 请求体发送前剥离
        if (!toolCalls.isEmpty())
            m.insert(QStringLiteral("tool_calls"), toolCalls);
        outMessages.append(m);
        outEventStart.append(ei);
        return true;
    }
    if (t == QLatin1String(kEvToolResult)) {
        outMessages.append(QJsonObject{
            {"role", QStringLiteral("tool")},
            {"tool_call_id", e.value(QStringLiteral("callId")).toString()},
            {"content", e.value(QStringLiteral("content")).toString()}});
        outEventStart.append(ei);
        return true;
    }
    return false; // turn/*、approval/* 不投影（审计/边界事件，模型不可见）
}

void ChatSession::deriveEvents(const QJsonArray &events, QJsonArray &outMessages,
                               QList<int> &outEventStart)
{
    outMessages = QJsonArray{};
    outEventStart.clear();

    // system：取最后一条 system/message（每轮重建语义，旧条目自然失效）
    QString sysText;
    for (const QJsonValue &v : events) {
        const QJsonObject e = v.toObject();
        if (evType(e) == QLatin1String(kEvSystem))
            sysText = e.value(QStringLiteral("text")).toString();
    }
    if (!sysText.isEmpty()) {
        outMessages.append(QJsonObject{{"role", QStringLiteral("system")},
                                       {"content", sysText}});
        outEventStart.append(-1); // system 消息无单一事件归属（截断永不动 system）
    }

    // 逐事件投影；assistant 块聚合
    for (int ei = 0; ei < events.size(); ++ei)
        deriveOneEvent(events.at(ei).toObject(), ei, outMessages, outEventStart);
}

void ChatSession::projectAppended(const QJsonObject &event, int ei)
{
    if (evType(event) == QLatin1String(kEvSystem)) {
        // 新 system 必为"最后一条"：替换投影头内容；投影头还没有 system
        //（装自无 system 的旧日志）时插到最前——罕见路径，整组重建一次
        const QString text = event.value(QStringLiteral("text")).toString();
        if (hasSystemIn(m_derived)) {
            m_derived.replace(0, QJsonObject{{"role", QStringLiteral("system")},
                                             {"content", text}});
        } else {
            QJsonArray msgs;
            msgs.append(QJsonObject{{"role", QStringLiteral("system")},
                                    {"content", text}});
            for (const QJsonValue &v : m_derived)
                msgs.append(v);
            m_derived = msgs;
            m_eventStartByMsg.prepend(-1);
        }
        return;
    }
    deriveOneEvent(event, ei, m_derived, m_eventStartByMsg);
}

void ChatSession::rederive()
{
    if (m_sessions.isEmpty()) {
        m_derived = QJsonArray{};
        m_eventStartByMsg.clear();
        return;
    }
    deriveEvents(active().events, m_derived, m_eventStartByMsg);
    active().messageCount =
        m_derived.size() - (hasSystemIn(m_derived) ? 1 : 0); // 侧栏口径缓存
}

// ---- 多会话管理 ----

QString ChatSession::activeSessionId() const
{
    return m_sessions.isEmpty() ? QString() : m_sessions[m_activeIndex].id;
}

QList<ChatSession::SessionSummary> ChatSession::sessionSummaries() const
{
    QList<SessionSummary> out;
    out.reserve(m_sessions.size());
    for (const SessionData &s : m_sessions) {
        // 侧栏"N 条"计数：投影消息数（排除 system），与主界面口径一致。
        // messageCount 缓存：活跃会话随 append/rederive 维护；冷会话（load
        // 后未活跃过）首次查询全量投影一次后回填（mutable 穿透 const），
        // 旧路径每次查询把所有会话全量重投影
        if (s.messageCount < 0) {
            QJsonArray derived;
            QList<int> start;
            deriveEvents(s.events, derived, start);
            s.messageCount = int(derived.size()) - (hasSystemIn(derived) ? 1 : 0);
        }
        out.append(SessionSummary{s.id, s.title, s.updatedAtMs, s.messageCount});
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
    rederive();
    emit changed();
    return s.id;
}

bool ChatSession::switchSession(const QString &id)
{
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i].id == id) {
            if (i != m_activeIndex) {
                m_activeIndex = i;
                rederive();
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
        rederive();
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
    rederive();
    emit changed();
    return true;
}

// ---- 消息操作域 API（index 为投影消息下标） ----

void ChatSession::truncateFrom(int index, bool includeIndex)
{
    const int count = m_eventStartByMsg.size();
    if (index < 0 || index >= count)
        return; // 越界：不动数据（调用方以消息数组快照取的 index，防御）
    const int evStart = m_eventStartByMsg.at(index);
    if (evStart < 0)
        return; // system 消息（无事件归属）：永不动
    const int start = includeIndex ? evStart : m_eventStartByMsg.at(index + 1 < count ? index + 1 : index);
    if (!includeIndex && index + 1 >= count)
        return; // 最后一条消息的 exclusive 截断 = 无事可做
    SessionData &s = active();
    while (s.events.size() > start)
        s.events.removeAt(s.events.size() - 1);
    // 截断后可能残留孤儿 turn 边界事件（原回合的 start/end 被留下）——
    // 投影忽略 turn 事件，无害；但为日志整洁，把流尾连续的孤儿 turn 清掉
    while (!s.events.isEmpty()) {
        const QString t = evType(s.events.last().toObject());
        if (t != QLatin1String(kEvTurnEnd) && t != QLatin1String(kEvTurnStart))
            break;
        s.events.removeAt(s.events.size() - 1);
    }
    touch();
    rederive();
    emit changed();
}

bool ChatSession::removeMessageAt(int index)
{
    const int count = m_eventStartByMsg.size();
    if (index < 0 || index >= count)
        return false;
    const int evStart = m_eventStartByMsg.at(index);
    if (evStart < 0)
        return false; // system：永不动

    // 事件删除区间：起始事件起，至下一条投影消息的起始事件前；assistant
    // (含 tool_call 块) 时天然覆盖其后配对的 tool/result 事件（它们是独立
    // 事件但位于区间内），防孤儿序列
    const int nextStart = index + 1 < count ? m_eventStartByMsg.at(index + 1)
                                            : active().events.size();
    SessionData &s = active();
    // 删除区间：assistant 消息连带其后配对的 tool/result 事件整组
    //（独立事件，但紧随其后——延伸到下一个非 tool/result 事件前，防孤儿）
    int end = nextStart;
    const bool isAssistant =
        m_derived.at(index).toObject().value(QStringLiteral("role")).toString()
        == QLatin1String("assistant");
    if (isAssistant) {
        while (end < s.events.size()
               && evType(s.events.at(end).toObject()) == QLatin1String(kEvToolResult))
            ++end;
    }
    for (int i = end - 1; i >= evStart; --i)
        s.events.removeAt(i);
    // 自愈：流尾孤儿 turn 边界清理（同 truncateFrom）
    while (!s.events.isEmpty()) {
        const QString t = evType(s.events.last().toObject());
        if (t != QLatin1String(kEvTurnEnd) && t != QLatin1String(kEvTurnStart))
            break;
        s.events.removeAt(s.events.size() - 1);
    }
    touch();
    rederive();
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
    QJsonArray &events = active().events;
    // 前置：流首孤儿 tool/result（前面没有 assistant 块的旧脏数据）没有任何
    // 回合块会收编它，trim 永远轮不到——单遍清点后一次性重建（旧路径逐条
    // removeAt + 每条一次全量重投影）
    int sysPrefixEnd = 0;
    while (sysPrefixEnd < events.size()
           && evType(events.at(sysPrefixEnd).toObject()) == QLatin1String(kEvSystem))
        ++sysPrefixEnd;
    int orphans = 0;
    while (sysPrefixEnd + orphans < events.size()
           && evType(events.at(sysPrefixEnd + orphans).toObject())
                  == QLatin1String(kEvToolResult))
        ++orphans;
    if (orphans > 0) {
        QJsonArray kept;
        for (int i = 0; i < events.size(); ++i) {
            if (i >= sysPrefixEnd && i < sysPrefixEnd + orphans)
                continue;
            kept.append(events.at(i));
        }
        events = kept;
        rederive();
    }

    // 投影消息 > kMaxHistory：从最旧**完整回合块**开始删除（回合块 = 一条
    // user/message 起至下一条 user/message 之前的全部事件，天然不拆
    // tool_call/result 对——dsh compaction 切点纪律）。system 消息永不动。
    // T-071：一次遍历记下全部回合块边界与块内投影消息数，贪心定出最小淘汰
    // 前缀后一次性重建 + 单次重投影（旧路径每淘汰一块就逐条 removeAt +
    // 全量重投影，淘汰 k 块 = O(k·n²)）
    const int maxTotal = kMaxHistory + (hasSystemIn(m_derived) ? 1 : 0);
    if (m_derived.size() <= maxTotal)
        return;
    struct TurnBlock
    {
        int begin; // 块内首条 user 事件下标
        int end;   // 块终点（exclusive）= 下一条 user 事件（或流尾）
        int msgs;  // 块内投影消息数（user + assistant + tool/result）
    };
    QList<TurnBlock> blocks;
    {
        int curBegin = -1;
        int userSeen = 0;
        for (int i = 0; i < events.size(); ++i) {
            if (evType(events.at(i).toObject()) != QLatin1String(kEvUser))
                continue;
            ++userSeen;
            if (curBegin >= 0)
                blocks.append({curBegin, i, 0});
            curBegin = i;
        }
        if (curBegin >= 0)
            blocks.append({curBegin, int(events.size()), 0});
        for (TurnBlock &b : blocks)
            b.msgs = countProjectedEvents(events, b.begin, b.end);
    }
    // 淘汰自最旧起，最后一格（最新块）永留——原防御口径（不足两个回合块不删，
    // 防误删尾部未完成回合）
    int cutEnd = -1;
    int removed = 0;
    for (int bi = 0; bi + 1 < blocks.size() && m_derived.size() - removed > maxTotal;
         ++bi) {
        removed += blocks.at(bi).msgs;
        cutEnd = blocks.at(bi + 1).begin;
    }
    if (cutEnd < 0)
        return;
    QJsonArray kept;
    // 头部 system 前缀永不动（首块起点之前的事件，当前恒为 system）
    const int headEnd = blocks.first().begin;
    for (int i = 0; i < headEnd; ++i)
        kept.append(events.at(i));
    // 淘汰区段 [首块起点, cutEnd) 整段跳过，其余保留
    for (int i = cutEnd; i < events.size(); ++i)
        kept.append(events.at(i));
    events = kept;
    rederive();
}

} // namespace poptools
