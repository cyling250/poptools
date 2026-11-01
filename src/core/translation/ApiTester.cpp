#include "ApiTester.h"
#include "Translator.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace poptools {

ApiTester::ApiTester(QObject *parent)
    : QObject(parent)
{
    // 超时看门狗必须是成员并随每次 test() 重启：原先用 singleShot 不随请求
    // 取消——上一轮快速失败后 10 秒内重测，旧定时器会 abort 新请求误报超时
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        if (m_reply)
            m_reply->abort();
    });
}

void ApiTester::test(const QString &baseUrl, const QString &apiKey, const QString &model,
                     ApiFormat format)
{
    if (m_busy)
        return;
    if (normalizeApiBaseUrl(baseUrl).isEmpty() || apiKey.trimmed().isEmpty()
        || model.trimmed().isEmpty()) {
        emit finished(false, QStringLiteral("请先填写 Base URL、API Key 和 Model"));
        return;
    }

    const QString base = normalizeApiBaseUrl(baseUrl);
    QNetworkRequest req(QUrl(base + apiEndpointPath(format, base)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    applyApiAuthHeaders(req, format, apiKey.trimmed());

    // 与 Translator 相同形态的最小请求，确保测的就是真实请求：
    //   OpenAI    → 含 thinking 禁用字段
    //   Anthropic → messages + 必填 max_tokens。不带 thinking 字段：部分端点
    //     （如官方 Anthropic）可能不识别 disabled 而拒收，而真实翻译请求被拒时
    //     会自动去掉字段降级重发，最终可用——测试只验证连通性/鉴权/模型名
    QByteArray payload;
    if (format == ApiFormat::Anthropic) {
        payload = QJsonDocument(QJsonObject{
            {"model", model.trimmed()},
            {"max_tokens", 16},
            {"messages", QJsonArray{QJsonObject{{"role", "user"}, {"content", "ping"}}}},
        }).toJson();
    } else {
        payload = QJsonDocument(QJsonObject{
            {"model", model.trimmed()},
            {"messages", QJsonArray{QJsonObject{{"role", "user"}, {"content", "ping"}}}},
            {"temperature", 0.2},
            {"thinking", QJsonObject{{"type", "disabled"}}},
        }).toJson();
    }

    m_busy = true;
    m_reply = m_nam.post(req, payload);
    m_timeout->start(10000); // 10 秒超时：中止请求（finished 按超时报告）

    connect(m_reply, &QNetworkReply::finished, this, [this] {
        auto *reply = m_reply;
        m_reply = nullptr;
        m_busy = false;
        m_timeout->stop();
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();

        if (reply->error() == QNetworkReply::OperationCanceledError) {
            emit finished(false, QStringLiteral("连接超时（10 秒无响应），请检查网络或代理"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError && status == 0) {
            // 未收到任何 HTTP 响应（DNS/连接/代理失败等）
            emit finished(false, QStringLiteral("无法连接到服务器：%1").arg(reply->errorString()));
            return;
        }

        // 尽量透出服务端 JSON 的 error.message（含"模型不存在""Key 无效"等具体原因）
        QString serverMsg;
        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (doc.isObject())
            serverMsg = doc.object().value("error").toObject().value("message").toString();

        if (status == 200) {
            emit finished(true, QStringLiteral("连接成功，API 可用"));
        } else if (status == 401 || status == 403) {
            emit finished(false, QStringLiteral("认证失败（%1）：API Key 无效或未授权，请重新复制粘贴").arg(status));
        } else if (status == 400 || status == 422) {
            emit finished(false, QStringLiteral("请求被拒绝（%1）：%2")
                              .arg(status)
                              .arg(serverMsg.isEmpty() ? reply->errorString() : serverMsg));
        } else {
            emit finished(false, QStringLiteral("服务端返回 %1：%2")
                              .arg(status)
                              .arg(serverMsg.isEmpty() ? reply->errorString() : serverMsg));
        }
    });
}

} // namespace poptools
