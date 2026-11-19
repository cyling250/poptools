#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QList>

class QTimer;

namespace poptools {

// assistant 消息块类型标记（blocks[].t），dsh ContentBlock 的 Qt 最小集：
// text（可见正文）/ reasoning（思考链，独立持久化）/ tool_call（模型发起的
// 工具调用，arguments 保留原始 JSON 串）。tool/result 走独立事件不走块。
namespace asst_blocks {
constexpr const char *kText = "text";
constexpr const char *kReasoning = "reasoning";
constexpr const char *kToolCall = "tool_call";
inline QJsonObject textBlock(const QString &text)
{
    return QJsonObject{{"t", kText}, {"text", text}};
}
inline QJsonObject reasoningBlock(const QString &text)
{
    return QJsonObject{{"t", kReasoning}, {"text", text}};
}
inline QJsonObject toolCallBlock(const QString &id, const QString &name,
                                 const QString &argsJson)
{
    return QJsonObject{{"t", kToolCall}, {"id", id}, {"name", name},
                       {"args", argsJson}};
}
} // namespace asst_blocks

// ============================================================
// goody 多轮会话（T-070 dsh 化：**会话即事件日志**）。
//
// 文件格式 version 3：{"version":3,"active":"<id>","sessions":[
//   {"id","title","updatedAt","events":[...]}]}
//
// events 为**追加式事件日志**（对齐 dsh SessionEvent），模型可见的一切
// 都必须能从日志重建（"模型可见即已记录"）；messages() 是投影而非存储：
//   turn/start|end            轮次边界（reason: completed|max-tokens|aborted|error）
//   system/message {text}     系统提示（投影取最后一条；内容变化才追加）
//   user/message {text}       用户消息
//   assistant/message {blocks,finish}  助手消息（块化：text/reasoning/
//                             tool_call，finish: stop|length|…）
//   tool/result {callId,content,isError}  工具结果（OpenAI tool 角色）
//   approval/asked {callId,name} / approval/decided {callId,decision}
//                             审批审计对（不投影进模型历史）
//
// version 2（messages 数组）装载时迁移：逐条消息转事件（user 起回合块，
// assistant 块化，tool 转 tool/result）；version 1 先按旧路径升 v2 再升 v3。
// 损坏仍走 .bad 备份 + 空载。
//
// 历史上限：投影消息 > 40 条时按**完整回合块**淘汰最旧（回合块 = 一条
// user/message 起至下一条 user/message 之前的全部事件）——切点不拆
// tool_call/result 对，对齐 dsh compaction 切点纪律。
// ============================================================
class ChatSession : public QObject
{
    Q_OBJECT
public:
    // storePath：sessions.json 全路径（注入便于单测）
    explicit ChatSession(const QString &storePath, QObject *parent = nullptr);

    void load(); // 文件缺失/损坏 → 空会话；v1/v2 → 迁移为 v3
    void save(); // QSaveFile 原子落盘
    // 防抖落盘：1s 内合并为一次写（工具密集回合的热路径用）；
    // 退出 flush 走显式 save()
    void saveSoon();

    // 投影消息数组（OpenAI 兼容，模型历史）——每次变更后重算的缓存。
    // GoodyModule 以此构造 LLM 请求
    const QJsonArray &messages() const { return m_derived; }
    int size() const { return messages().size(); }

    // ---- 事件 API（追加进当前会话日志） ----
    void turnStart();
    void turnEnd(const QString &reason);
    // 系统提示：与当前日志最后一条 system/message 内容相同则不追加
    //（对齐 dsh"变化才替换"，保住请求前缀）
    void setSystemPrompt(const QString &prompt);
    void appendUserMessage(const QString &text);
    // blocks 见 asst_blocks 命名空间；finish 为 finish_reason（stop/length/…）
    void appendAssistantBlocks(const QJsonArray &blocks, const QString &finish);
    void appendToolResult(const QString &callId, const QString &content, bool isError);
    void appendApprovalAsked(const QString &callId, const QString &name);
    void appendApprovalDecided(const QString &callId, const QString &decision);
    void clear(); // 当前会话全部清空（事件日志清零，标题复位）

    // ---- 多会话管理 ----
    // 侧栏条目摘要（按 updatedAt 降序 = 最近活跃在前）
    struct SessionSummary
    {
        QString id;
        QString title;
        qint64 updatedAtMs = 0;
        int messageCount = 0;
    };
    QString activeSessionId() const;
    QList<SessionSummary> sessionSummaries() const;
    QString createSession();               // 新建空会话并切换为当前，返回 id
    bool switchSession(const QString &id); // 切换当前会话
    // 删除会话；删除当前会话时自动切到最近活跃的余下会话（无余下则新建
    // 空会话），保证任何时刻 active 恒有效
    bool deleteSession(const QString &id);

    // ---- 消息操作四件套的域 API（index 均为**投影消息下标**） ----
    // 从 index 起截断（includeIndex=true 连 index 本身一并丢弃）——
    // "编辑重发"=对目标 user 消息 inclusive 截断，其后事件全部丢弃
    void truncateFrom(int index, bool includeIndex);
    // 删除单条消息；assistant(含 tool_call 块) 连带其后配对的 tool/result
    // 事件整组删除（防孤儿序列）。删除越界/非法 → 返回 false 不动数据
    bool removeMessageAt(int index);

    // 会话自动标题：首条用户消息折叠空白后截 ~24 字（不额外花 API 调用）
    static QString autoSessionTitle(const QString &firstUserText);

signals:
    // 会话列表/标题/消息任何变化（UI 侧栏与气泡列表刷新依据）。
    // setSystemPrompt 内容未变化时不发（与侧栏无关）
    void changed();

private:
    struct SessionData
    {
        QString id;
        QString title;
        qint64 updatedAtMs = 0;
        QJsonArray events; // 追加式事件日志（v3 存储）
    };

    SessionData &active() { return m_sessions[m_activeIndex]; }
    const SessionData &active() const { return m_sessions[m_activeIndex]; }
    void ensureDefaultSession(); // 装载后保证至少一个会话且 active 指向有效项
    void touch();                // 活跃时间戳刷新（append 系入口共用）
    void appendEvent(const QJsonObject &event); // 追加事件 + 触摸 + 重投影 + changed
    void rederive();             // 事件日志 → OpenAI 消息投影 + 消息起点索引
    void trim();                 // 投影超限时按完整回合块淘汰最旧事件
    // 从事件数组重投影（load/迁移/单测共用；q事件 → m_derived/m_eventStartByMsg）
    static void deriveEvents(const QJsonArray &events, QJsonArray &outMessages,
                             QList<int> &outEventStart);

    QString m_path;
    QList<SessionData> m_sessions;
    int m_activeIndex = 0;
    QJsonArray m_derived;        // 投影缓存（当前会话）
    QList<int> m_eventStartByMsg; // 投影消息 i 的起始事件下标（四件套/trim 用）
    QTimer *m_saveTimer = nullptr; // 防抖落盘定时器（单发）
};

} // namespace poptools
