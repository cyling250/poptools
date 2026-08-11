#include "Translator.h"
#include "core/settings/Settings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrl>

namespace snipaste {

namespace {

// 语言代码 → 提示词中的自然语言名称（模型更易理解）
QString langName(const QString &code)
{
    if (code == "zh") return "Chinese";
    if (code == "en") return "English";
    if (code == "ja") return "Japanese";
    if (code == "ko") return "Korean";
    if (code == "fr") return "French";
    if (code == "de") return "German";
    if (code == "es") return "Spanish";
    if (code == "ru") return "Russian";
    if (code == "auto") return "the source language (detect it from the text)";
    return code; // 用户自定义（如 "pt"）
}

// 专业翻译提示词：告知文本由 OCR 从截图提取（可能有错字/标点缺失/夹杂 UI 文案），
// 要求按实际语境翻译且只输出翻译结果，不附带任何其他内容。
QString buildPrompt(const QString &text, const QString &fromLang, const QString &toLang)
{
    return QStringLiteral(
               "You are a professional translation engine. The text below was extracted from a "
               "screenshot via OCR, so it may contain typos, missing or incorrect punctuation, or "
               "stray content such as UI labels, numbers, or code. Translate the text from %1 to %2 "
               "according to its actual context and meaning. Output ONLY the translated text — no "
               "explanations, no notes, no extra content.\n\n%3")
        .arg(langName(fromLang), langName(toLang), text);
}

} // namespace

Translator::Translator(QObject *parent)
    : QObject(parent)
{
}

void Translator::translate(const QString &text, const QString &fromLang, const QString &toLang)
{
    if (m_busy || text.trimmed().isEmpty())
        return;
    translateByApi(text, fromLang, toLang);
}

void Translator::translateByApi(const QString &text, const QString &fromLang, const QString &toLang)
{
    const QString baseUrl = Settings::instance().apiBaseUrl();
    const QString apiKey  = Settings::instance().apiKey();
    const QString model   = Settings::instance().apiModel();

    if (baseUrl.isEmpty() || apiKey.isEmpty()) {
        m_busy = false;
        emit translateFinished(false, QString(), "未配置 API（base_url / api_key）");
        return;
    }

    QUrl url(baseUrl + "/chat/completions");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());

    // 单问答形式（messages 只有 system + user 两条，无历史上下文）
    QJsonObject msg1{
        {"role", "system"},
        {"content", buildPrompt(text, fromLang, toLang)},
    };
    QJsonObject body{
        {"model", model},
        {"messages", QJsonArray{msg1}},
        {"temperature", 0.2},
    };

    m_busy = true;
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

} // namespace snipaste
