#include "LlmClient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "../translation/Translator.h" // normalizeApiBaseUrl/apiEndpointPath/applyApiAuthHeaders

namespace poptools {

// ---------- LlmStreamParser ----------

QString LlmStreamParser::feed(const QByteArray &bytes)
{
    m_buf += bytes;
    QString delta;
    int nl = -1;
    // SSE 事件以 \n 分行；半行留在缓冲等下一块
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        QByteArray line = m_buf.left(nl);
        m_buf.remove(0, nl + 1);
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        if (!line.startsWith("data:"))
            continue; // 忽略注释/其他字段行
        QByteArray payload = line.mid(5).trimmed();
        if (payload == "[DONE]") {
            m_done = true;
            break;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject())
            continue;
        const QJsonObject obj = doc.object();
        if (obj.contains(QStringLiteral("error"))) {
            // 流中带错误对象（部分网关在 200 后才吐业务错）：记入 finishReason 供上层报错
            m_finishReason = QStringLiteral("error:%1")
                                 .arg(obj.value(QStringLiteral("error"))
                                          .toObject()
                                          .value(QStringLiteral("message"))
                                          .toString());
            m_done = true;
            break;
        }
        const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty())
            continue;
        const QJsonObject choice = choices.first().toObject();
        if (choice.contains(QStringLiteral("finish_reason"))
            && !choice.value(QStringLiteral("finish_reason")).isNull())
            m_finishReason = choice.value(QStringLiteral("finish_reason")).toString();
        const QJsonObject deltaObj = choice.value(QStringLiteral("delta")).toObject();
        const QString content = deltaObj.value(QStringLiteral("content")).toString();
        if (!content.isEmpty())
            delta += content;
        m_content += content;
        // T-050-P2：思考内容增量（reasoning_content）聚合，照 tool_calls 先例；
        // 逐块暂存 pending，上层经 takeReasoningDelta() 取走后发信号驱动
        // 思考折叠区实时展示
        const QString reasoning = deltaObj.value(QStringLiteral("reasoning_content")).toString();
        if (!reasoning.isEmpty()) {
            m_reasoning += reasoning;
            m_reasoningPending += reasoning;
        }
        // 工具调用分片：按 index 聚合 id/name，arguments 字符串拼接
        const QJsonArray tcs = deltaObj.value(QStringLiteral("tool_calls")).toArray();
        for (const QJsonValue &tc : tcs) {
            const QJsonObject t = tc.toObject();
            const int index = t.value(QStringLiteral("index")).toInt();
            if (!m_toolCalls.contains(index)) {
                m_toolCalls.insert(index, ToolAccum{});
                m_toolOrder.append(index);
            }
            ToolAccum &acc = m_toolCalls[index];
            if (!t.value(QStringLiteral("id")).isNull() && !t.value(QStringLiteral("id")).toString().isEmpty())
                acc.id = t.value(QStringLiteral("id")).toString();
            const QJsonObject fn = t.value(QStringLiteral("function")).toObject();
            const QString fnName = fn.value(QStringLiteral("name")).toString();
            if (!fnName.isEmpty())
                acc.name = fnName;
            acc.arguments += fn.value(QStringLiteral("arguments")).toString();
        }
    }
    return delta;
}

QString LlmStreamParser::takeReasoningDelta()
{
    const QString out = m_reasoningPending;
    m_reasoningPending.clear();
    return out;
}

QJsonObject LlmStreamParser::assistantMessage() const
{
    QJsonObject msg{{"role", QStringLiteral("assistant")},
                    {"content", m_content}};
    if (!m_reasoning.isEmpty())
        msg.insert(QStringLiteral("reasoning"), m_reasoning);
    if (!m_toolCalls.isEmpty()) {
        QJsonArray arr;
        for (int index : m_toolOrder) {
            const ToolAccum &acc = m_toolCalls.value(index);
            arr.append(QJsonObject{
                {"id", acc.id.isEmpty() ? QStringLiteral("call_%1").arg(index) : acc.id},
                {"type", QStringLiteral("function")},
                {"function", QJsonObject{{"name", acc.name},
                                         {"arguments", acc.arguments.isEmpty()
                                                           ? QStringLiteral("{}")
                                                           : acc.arguments}}},
            });
        }
        msg.insert(QStringLiteral("tool_calls"), arr);
    }
    return msg;
}

// ---------- LlmClient ----------

LlmClient::LlmClient(QObject *parent)
    : QObject(parent)
{
}

bool LlmClient::chat(const QString &baseUrl, const QString &apiKey, const QString &model,
                     const QJsonArray &messages, const QJsonArray &tools)
{
    if (m_reply)
        return false; // 忙：调用方须等 finished

    const QString base = normalizeApiBaseUrl(baseUrl);
    QNetworkRequest req(base + apiEndpointPath(ApiFormat::OpenAI, base));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    applyApiAuthHeaders(req, ApiFormat::OpenAI, apiKey);
    // 流式响应持续吐 token，90s 无数据视为断流（含首 token 冷启动余量）
    req.setTransferTimeout(90 * 1000);

    // 整体重置解析器：上一轮残留的半行缓冲/tool 聚合/reasoning pending
    // 不得泄入本轮（单飞正确性的关键前置）
    m_parser = LlmStreamParser{};
    m_userAborted = false; // T-062 修复: 新回合复位中止标志（上轮 abort 不得污染本轮归因）
    m_reply = m_nam.post(req, buildChatRequestBody(model, messages, tools, true));

    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (!m_reply)
            return;
        const QString delta = m_parser.feed(m_reply->readAll());
        if (!delta.isEmpty())
            emit contentDelta(delta);
        // T-050-P2：思考增量透传（流式思考折叠区）。局部变量勿与信号同名
        const QString rDelta = m_parser.takeReasoningDelta();
        if (!rDelta.isEmpty())
            emit reasoningDelta(rDelta);
    });
    connect(m_reply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError) {
        // 留给 finished 统一收尾：流可能已出过部分正文
    });
    connect(m_reply, &QNetworkReply::finished, this, [this] { cleanupReply(); });

    return true;
}

void LlmClient::abort()
{
    // T-062 修复: 置位用户中止标志再 abort——90s 传输超时同样产生
    // OperationCanceledError，无标志则断流会被误归因为"已中止"
    m_userAborted = true;
    if (m_reply)
        m_reply->abort(); // finished 随之触发统一收尾
}

// 单飞收尾：reply 脱钩 + deleteLater，三分支归一——流内业务错(error:*)/
// 网络错(带响应体摘要 1KB)/用户中止(交回部分内容保持上下文)，正常结束
// 交完整助手消息。finished 恒发一次
void LlmClient::cleanupReply()
{
    QNetworkReply *reply = m_reply;
    if (!reply)
        return;
    m_reply = nullptr;
    reply->deleteLater();

    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // T-062 修复: 归因改为"用户中止标志 + 取消错误码"双条件——纯错误码
    // 分不清用户 abort() 与 90s 传输超时（两者同为 OperationCanceledError），
    // 断流曾因此被误报"已中止"。超时现落入下方网络错分支，报出真实错误
    const bool aborted =
        m_userAborted && reply->error() == QNetworkReply::OperationCanceledError;
    const bool streamDone = m_parser.done();
    const QString streamErr = m_parser.finishReason();
    if (streamErr.startsWith(QLatin1String("error:"))) {
        // 上游在流内吐了业务错误
        emit finished(false, QJsonObject{}, streamErr.mid(6));
        return;
    }

    if (!streamDone && reply->error() != QNetworkReply::NoError && !aborted) {
        QString detail = reply->errorString();
        const QByteArray body = reply->peek(1024 * 4);
        if (!body.isEmpty())
            detail += QStringLiteral(" | %1").arg(QString::fromUtf8(body.left(1024)));
        emit finished(false, QJsonObject{},
                      QStringLiteral("HTTP %1：%2").arg(httpStatus).arg(detail));
        return;
    }
    if (aborted && !streamDone) {
        // 用户中止：交回已生成的部分内容（消息仍可入会话，保持上下文连续）
        QJsonObject msg = m_parser.assistantMessage();
        msg.insert(QStringLiteral("aborted"), true);
        emit finished(false, msg, QStringLiteral("已中止"));
        return;
    }

    emit finished(true, m_parser.assistantMessage(), QString());
}

} // namespace poptools
