#include "Translator.h"
#include "Settings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrl>

Translator::Translator(QObject *parent)
    : QObject(parent)
{
}

void Translator::translate(const QString &text)
{
    if (m_busy || text.trimmed().isEmpty())
        return;

    m_busy = true;
    if (Settings::instance().translateMode() == "local") {
        QString out;
        if (translateLocal(text, out))
            emit translateFinished(true, out, QString());
        else
            emit translateFinished(false, QString(), "本地翻译引擎未配置");
        m_busy = false;
        return;
    }
    translateByApi(text);
}

// 本地引擎：预留接口（例如后续接入词典型/本地模型）
bool Translator::translateLocal(const QString &text, QString &out)
{
    Q_UNUSED(text)
    Q_UNUSED(out)
    return false;
}

void Translator::translateByApi(const QString &text)
{
    const QString baseUrl = Settings::instance().apiBaseUrl();
    const QString apiKey  = Settings::instance().apiKey();
    const QString model   = Settings::instance().apiModel();
    const QString target  = Settings::instance().targetLang();

    if (baseUrl.isEmpty() || apiKey.isEmpty()) {
        m_busy = false;
        emit translateFinished(false, QString(), "未配置 API（base_url / api_key）");
        return;
    }

    QUrl url(baseUrl + "/chat/completions");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());

    QJsonObject msg1{
        {"role", "system"},
        {"content", QString("You are a translation engine. Translate the user's text into %1. Output only the translation, no explanations.").arg(target)},
    };
    QJsonObject msg2{
        {"role", "user"},
        {"content", text},
    };
    QJsonObject body{
        {"model", model},
        {"messages", QJsonArray{msg1, msg2}},
        {"temperature", 0.2},
    };

    m_reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(m_reply, &QNetworkReply::finished, this, &Translator::onReplyFinished);
}

void Translator::onReplyFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;

    reply->deleteLater();
    m_busy = false;

    if (reply->error() != QNetworkReply::NoError) {
        emit translateFinished(false, QString(), reply->errorString());
        return;
    }

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &perr);
    if (perr.error != QJsonParseError::NoError) {
        emit translateFinished(false, QString(), "响应解析失败: " + perr.errorString());
        return;
    }

    const QString result = doc.object()
                               .value("choices").toArray()
                               .at(0).toObject()
                               .value("message").toObject()
                               .value("content").toString()
                               .trimmed();

    if (result.isEmpty())
        emit translateFinished(false, QString(), "API 返回空结果");
    else
        emit translateFinished(true, result, QString());
}
