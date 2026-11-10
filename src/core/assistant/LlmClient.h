#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

class QNetworkReply;

namespace poptools {

// 构造 OpenAI 兼容 /chat/completions 请求体（纯函数，单测覆盖）。
// messages 为完整对话数组；tools 为 OpenAI 工具 schema 数组（可空）。
// stream=true：SSE 流式（goody 聊天必须流式，等待感是体验底线）
inline QByteArray buildChatRequestBody(const QString &model, const QJsonArray &messages,
                                       const QJsonArray &tools, bool stream)
{
    QJsonObject body{
        {"model", model},
        {"messages", messages},
        {"temperature", 0.3},
        {"stream", stream},
    };
    if (!tools.isEmpty())
        body.insert("tools", tools);
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

// ============================================================
// SSE 流解析器（纯逻辑，不碰网络，单测覆盖）。
//
// DeepSeek/OneAPI 系的 chat.completions 流格式：
//   data: {"choices":[{"delta":{"content":"…"},...}],...}\n
//   data: {"choices":[{"delta":{"tool_calls":[{"index":0,"id":"…",
//           "function":{"name":"…","arguments":"片段"}}]}}]}\n
//   data: [DONE]\n
// 工具调用的 arguments 按 index 分片下发，此处聚合为完整 JSON 字符串。
// ============================================================
class LlmStreamParser
{
public:
    // 喂入一块网络字节（可能含半行）；返回本次解析出的正文增量
    QString feed(const QByteArray &bytes);

    bool done() const { return m_done; }        // 收到 [DONE]
    bool hasToolCalls() const { return !m_toolCalls.isEmpty(); }
    QString finishReason() const { return m_finishReason; }

    // 完成后的助手消息（可直接 append 进对话数组）：
    //   {"role":"assistant","content":"…"[,"tool_calls":[{id,type,function}]]}
    QJsonObject assistantMessage() const;

private:
    QByteArray m_buf;               // 半行残留
    QString m_content;              // 正文累积
    QString m_finishReason;
    bool m_done = false;

    struct ToolAccum
    {
        QString id, name, arguments;
    };
    QHash<int, ToolAccum> m_toolCalls; // 按 index 聚合
    QList<int> m_toolOrder;
};

// ============================================================
// goody 的 LLM 客户端：OpenAI 兼容 /chat/completions + SSE 流式 + 工具调用。
//
// 复用 Translator.h 的 normalizeApiBaseUrl / apiEndpointPath /
// applyApiAuthHeaders（OpenAI 分支）；与 Translator 一样跑在主线程
// 事件循环上（QNAM 异步，无阻塞调用）。一轮对话可能产生多次"工具
// 调用回合"，回合循环由调用方（GoodyModule）编排：本类只负责单次
// 请求：发消息数组 → 流式吐增量 → 结束时交回完整助手消息。
// ============================================================
class LlmClient : public QObject
{
    Q_OBJECT
public:
    explicit LlmClient(QObject *parent = nullptr);

    // 发起一轮对话（异步）。messages 为完整数组；tools 可空。
    // 重复调用前须等上一轮 finished（忙碌时返回 false）
    bool chat(const QString &baseUrl, const QString &apiKey, const QString &model,
              const QJsonArray &messages, const QJsonArray &tools = QJsonArray());

    void abort(); // 用户中止
    bool busy() const { return m_reply != nullptr; }

signals:
    // 正文增量（流式渲染用）
    void contentDelta(const QString &delta);
    // 一轮结束：ok 时 assistantMessage 为完整助手消息（content 与/或
    // tool_calls），error 为人话错误（含 HTTP 状态与响应摘要）
    void finished(bool ok, const QJsonObject &assistantMessage, const QString &error);

private:
    void cleanupReply();

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    LlmStreamParser m_parser;
};

} // namespace poptools
