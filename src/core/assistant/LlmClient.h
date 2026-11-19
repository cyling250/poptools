#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include "ChatSession.h" // asst_blocks 块标记（T-070 块化输出）

class QNetworkReply;

namespace poptools {

// 构造 OpenAI 兼容 /chat/completions 请求体（纯函数，单测覆盖）。
// messages 为完整对话数组；tools 为 OpenAI 工具 schema 数组（可空）。
// stream=true：SSE 流式（goody 聊天必须流式，等待感是体验底线）。
// T-050-P2：assistant 消息的 reasoning 字段（思考内容，仅本地持久化与
// 展示）在此剥离——网关对 message 内非标准字段的容忍度无文档承诺，
// 请求体只携带标准 OpenAI 字段。
inline QByteArray buildChatRequestBody(const QString &model, const QJsonArray &messages,
                                       const QJsonArray &tools, bool stream)
{
    QJsonArray cleanMessages;
    for (const QJsonValue &v : messages) {
        QJsonObject m = v.toObject();
        if (m.value(QStringLiteral("role")).toString() == QLatin1String("assistant"))
            m.remove(QStringLiteral("reasoning"));
        cleanMessages.append(m);
    }
    QJsonObject body{
        {"model", model},
        {"messages", cleanMessages},
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
//   data: {"choices":[{"delta":{"reasoning_content":"思考增量"}}]}\n
//   data: {"choices":[{"delta":{"tool_calls":[{"index":0,"id":"…",
//           "function":{"name":"…","arguments":"片段"}}]}}]}\n
//   data: [DONE]\n
// 工具调用的 arguments 按 index 分片下发，此处聚合为完整 JSON 字符串。
// T-050-P2：reasoning_content 思考增量照 tool_calls 先例聚合，经
// takeReasoningDelta() 逐块取走供 UI 实时展示；完成后随 assistantMessage
// 以 reasoning 字段落会话（请求体发送前由 buildChatRequestBody 剥离）。
// ============================================================
class LlmStreamParser
{
public:
    // 喂入一块网络字节（可能含半行）；返回本次解析出的正文增量
    QString feed(const QByteArray &bytes);

    bool done() const { return m_done; }        // 收到 [DONE]
    bool hasToolCalls() const { return !m_toolCalls.isEmpty(); }
    QString finishReason() const { return m_finishReason; }

    // 思考内容（reasoning_content）聚合与增量取走
    QString reasoning() const { return m_reasoning; }
    QString takeReasoningDelta();               // 取走并清零未消费的思考增量

    // 完成后的助手消息（可直接 append 进对话数组）：
    //   {"role":"assistant","content":"…"[,"reasoning":"…"]
    //    [,"tool_calls":[{id,type,function}]]}
    // T-070 dsh 块化：另携 "blocks"（asst_blocks 约定的 text/reasoning/
    // tool_call 块数组，arguments 保留原始 JSON 串）与 "finish"（finish_
    // reason；供回合收束判据 max-tokens 粘性）
    QJsonObject assistantMessage() const;

    // 块数组单独取用（blocks 与 assistantMessage().blocks 同源）
    QJsonArray blocks() const;

private:
    QByteArray m_buf;               // 半行残留
    QString m_content;              // 正文累积
    QString m_reasoning;            // 思考内容累积（T-050-P2）
    QString m_reasoningPending;     // 尚未被 takeReasoningDelta 取走的增量
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

    void abort(); // 用户中止（与 90s 传输超时的区分见 m_userAborted）
    bool busy() const { return m_reply != nullptr; }

signals:
    // 正文增量（流式渲染用）
    void contentDelta(const QString &delta);
    // 思考内容增量（reasoning_content，流式渲染思考折叠区用，T-050-P2）
    void reasoningDelta(const QString &delta);
    // 一轮结束：ok 时 assistantMessage 为完整助手消息（content 与/或
    // tool_calls，含 reasoning），error 为人话错误（含 HTTP 状态与响应摘要）
    void finished(bool ok, const QJsonObject &assistantMessage, const QString &error);

private:
    // 单飞收尾：reply 脱钩 + deleteLater，三分支归一——流内业务错(error:*)/
    // 网络错(带响应体摘要 1KB)/用户中止(交回部分内容保持上下文)，正常结束
    // 交完整助手消息。finished 恒发一次
    void cleanupReply();

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    LlmStreamParser m_parser;
    // T-062 修复: 90s 传输超时与用户 abort() 都以 OperationCanceledError
    // 收尾，单靠 reply->error() 无法区分——断流曾被误报为"已中止"。此标志
    // 只在 abort() 置位、chat() 发起时复位，归因以"标志+错误码"双条件判定
    bool m_userAborted = false;
};

} // namespace poptools
