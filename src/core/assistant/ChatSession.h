#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QTimer;

namespace poptools {

// ============================================================
// goody 多轮会话：消息数组 + QSaveFile 持久化（goody/chat/sessions.json）。
//
// 结构约定：messages[0] 恒为 system（人格+上下文注入，每轮由模块
// 重建），其后为 user / assistant（含 tool_calls）/ tool 交替。
// 历史上限 40 条（不含 system）：超限从头丢弃最旧的非系统消息，
// 控制每次请求的 token 体量（DeepSeek 按 token 计费，聊天上下文
// 无限增长会烧钱且变慢）。
// ============================================================
class ChatSession : public QObject
{
    Q_OBJECT
public:
    // storePath：sessions.json 全路径（注入便于单测）
    explicit ChatSession(const QString &storePath, QObject *parent = nullptr);

    void load(); // 文件缺失/损坏 → 空会话
    void save(); // QSaveFile 原子落盘
    // 防抖落盘：1s 内合并为一次写（工具密集回合的热路径用）；
    // 退出 flush 走显式 save()
    void saveSoon();

    const QJsonArray &messages() const { return m_messages; }
    int size() const { return m_messages.size(); }

    // 每轮请求前调用：重建 system 消息（无则插入）
    void setSystemPrompt(const QString &prompt);

    void append(const QJsonObject &msg);          // user / assistant
    void appendToolResult(const QString &toolCallId, const QJsonObject &result); // role=tool
    void clear();                                 // 全部清空（含 system，下轮重建）

private:
    void trim();

    QString m_path;
    QJsonArray m_messages;
    QTimer *m_saveTimer = nullptr; // 防抖落盘定时器（单发）
};

} // namespace poptools
