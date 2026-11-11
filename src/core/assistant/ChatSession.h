#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QList>

class QTimer;

namespace poptools {

// ============================================================
// goody 多轮会话：多会话数组 + QSaveFile 持久化（goody/chat/sessions.json）。
//
// T-050-P2 升级为多会话：文件格式 version 2：
//   {"version":2,"active":"<id>","sessions":[{"id","title","updatedAt",
//                                             "messages":[...]}]}
// version 1 旧文件（单 messages 数组）装载时无损迁移为单个会话
//（标题取首条用户消息自动摘要）；损坏仍走 .bad 备份 + 空载。
//
// 结构约定：messages[0] 恒为 system（人格+上下文注入，每轮由模块
// 重建），其后为 user / assistant（含 tool_calls / reasoning）/ tool
// 交替。assistant 的 reasoning 字段（思考内容，T-050-P2）仅本地持久化，
// 发送请求体前由 buildChatRequestBody 剥离。
// 每会话历史上限 40 条（不含 system）：超限从头丢弃最旧的非系统消息，
// 控制每次请求的 token 体量（DeepSeek 按 token 计费，聊天上下文
// 无限增长会烧钱且变慢）。
// ============================================================
class ChatSession : public QObject
{
    Q_OBJECT
public:
    // storePath：sessions.json 全路径（注入便于单测）
    explicit ChatSession(const QString &storePath, QObject *parent = nullptr);

    void load(); // 文件缺失/损坏 → 空会话；v1 → 迁移为单会话
    void save(); // QSaveFile 原子落盘
    // 防抖落盘：1s 内合并为一次写（工具密集回合的热路径用）；
    // 退出 flush 走显式 save()
    void saveSoon();

    const QJsonArray &messages() const { return m_sessions[m_activeIndex].messages; }
    int size() const { return messages().size(); }

    // 每轮请求前调用：重建 system 消息（无则插入）
    void setSystemPrompt(const QString &prompt);

    void append(const QJsonObject &msg);          // user / assistant
    void appendToolResult(const QString &toolCallId, const QJsonObject &result); // role=tool
    void clear();                                 // 当前会话全部清空（含 system，下轮重建）

    // ---- T-050-P2：多会话管理 ----
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

    // ---- T-050-P2：消息操作四件套的域 API ----
    // 从 index 起截断（includeIndex=true 连 index 本身一并丢弃）——
    // "编辑重发"=对目标 user 消息 inclusive 截断，其后消息全部丢弃
    void truncateFrom(int index, bool includeIndex);
    // 删除单条消息；目标是 assistant(tool_calls) 时连带其后配对的 tool
    // 回应整组删除（否则孤儿 tool 会让之后每轮请求被 API 整包拒绝）。
    // 删除越界/非法 → 返回 false 不动数据
    bool removeMessageAt(int index);

    // 会话自动标题：首条用户消息折叠空白后截 ~24 字（不额外花 API 调用）
    static QString autoSessionTitle(const QString &firstUserText);

signals:
    // 会话列表/标题/消息任何变化（UI 侧栏与气泡列表刷新依据）。
    // setSystemPrompt 不发（每轮重建 system 与侧栏无关）
    void changed();

private:
    struct SessionData
    {
        QString id;
        QString title;
        qint64 updatedAtMs = 0;
        QJsonArray messages;
    };

    SessionData &active() { return m_sessions[m_activeIndex]; }
    const SessionData &active() const { return m_sessions[m_activeIndex]; }
    void ensureDefaultSession(); // 装载后保证至少一个会话且 active 指向有效项
    void touch();                // 活跃时间戳刷新（append 系入口共用）
    void trim();

    QString m_path;
    QList<SessionData> m_sessions;
    int m_activeIndex = 0;
    QTimer *m_saveTimer = nullptr; // 防抖落盘定时器（单发）
};

} // namespace poptools
