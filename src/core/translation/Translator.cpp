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
// 翻译是轻量任务，明确禁止输出思考过程（配合请求体 thinking 禁用字段双保险）。
QString buildBasePrompt(const QString &fromLang, const QString &toLang, bool isOcr)
{
    QString prompt;
    if (isOcr) {
        prompt = QStringLiteral(
            "You are a professional translation engine. The text below was extracted from a "
            "screenshot via OCR, so it may contain typos, missing or incorrect punctuation, or "
            "stray content such as UI labels, numbers, or code. Translate the text from %1 to %2 "
            "according to its actual context and meaning. Output ONLY the translated text — no "
            "explanations, no notes, no reasoning, no <think> tags, no extra content.");
    } else {
        // 划词翻译：文本直接来自用户选中，无 OCR 噪声
        prompt = QStringLiteral(
            "You are a professional translation engine. Translate the text below from %1 to %2. "
            "Output ONLY the translated text — no explanations, no notes, no reasoning, "
            "no <think> tags, no extra content.");
    }
    return prompt.arg(langName(fromLang), langName(toLang));
}

// 用户在设置中配置的翻译领域与专有名词术语表 → 追加到系统提示词。
// 两项均未配置时返回空串，提示词保持最简。
QString buildDomainAndTermsPrompt()
{
    const Settings &s = Settings::instance();
    QStringList blocks;

    const QStringList domains = s.translateDomains();
    if (!domains.isEmpty()) {
        blocks += QStringLiteral(
                      "The text belongs to the following subject domains: %1. Use the standard "
                      "terminology and writing conventions of these domains.")
                      .arg(domains.join(QStringLiteral(", ")));
    }

    const QList<Settings::TermPair> terms = s.translateTerms();
    if (!terms.isEmpty()) {
        QString t = QStringLiteral(
            "Terminology mapping — when these source terms appear, use exactly the required "
            "translation (source -> required translation):");
        for (const Settings::TermPair &term : terms)
            t += QStringLiteral("\n- %1 -> %2").arg(term.first, term.second);
        blocks += t;
    }

    return blocks.isEmpty() ? QString() : QStringLiteral("\n\n") + blocks.join(QLatin1Char('\n'));
}

} // namespace

Translator::Translator(QObject *parent)
    : QObject(parent)
{
}

void Translator::translate(const QString &text, const QString &fromLang, const QString &toLang, bool isOcr)
{
    if (m_busy || text.trimmed().isEmpty())
        return;

    // 保存待翻译参数：若端点拒绝 thinking 禁用字段，降级重发时复用
    m_pendingText = text;
    m_pendingFrom = fromLang;
    m_pendingTo = toLang;
    m_pendingIsOcr = isOcr;
    m_tryDisableThinking = true;
    translateByApi(text, fromLang, toLang, isOcr, true);
}

void Translator::translateByApi(const QString &text, const QString &fromLang, const QString &toLang,
                                bool isOcr, bool disableThinking)
{
    const QString baseUrl = Settings::instance().apiBaseUrl();
    const QString apiKey  = Settings::instance().apiKey();
    const QString model   = Settings::instance().apiModel();

    if (baseUrl.isEmpty() || apiKey.isEmpty()) {
        m_busy = false;
        emit translateFinished(false, QString(), "未配置 API（base_url / api_key）");
        return;
    }

    QUrl url(normalizeApiBaseUrl(baseUrl) + "/chat/completions");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());

    // 单问答形式（messages 只有 system + user 两条，无历史上下文）
    QJsonObject msg1{
        {"role", "system"},
        {"content", buildBasePrompt(fromLang, toLang, isOcr) + buildDomainAndTermsPrompt()},
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
    if (disableThinking) {
        // 显式关闭思考模式（DeepSeek V4 实测：thinking.type 合法值为
        // adaptive/enabled/disabled，翻译是短平快任务，思维链只会拖慢出字。
        // 端点不识别该字段时按 400/422 降级重发）
        body.insert("thinking", QJsonObject{{"type", "disabled"}});
    }

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
    m_reply = nullptr;

    // 端点不认识 thinking 禁用字段（严格校验的 OpenAI 兼容服务返回 400/422）：
    // 去掉该字段降级重发一次，仅损失"关思考"优化，不影响翻译可用性
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError && m_tryDisableThinking
        && (httpStatus == 400 || httpStatus == 422)) {
        m_tryDisableThinking = false;
        translateByApi(m_pendingText, m_pendingFrom, m_pendingTo, m_pendingIsOcr, false);
        return; // 保持 m_busy，等待重发请求完成
    }

    m_busy = false;

    const QByteArray raw = reply->readAll();

    if (reply->error() != QNetworkReply::NoError) {
        // 把晦涩的 Qt 错误串翻译成可读原因：优先透出服务端 JSON 的 error.message
        //（如"Key 无效""模型不存在"）。注意 Qt 将 HTTP 401 一律映射为
        // "Host requires authentication"，必须转成人话
        QString serverMsg;
        const QJsonDocument errDoc = QJsonDocument::fromJson(raw);
        if (errDoc.isObject())
            serverMsg = errDoc.object().value("error").toObject().value("message").toString();

        QString msg;
        if (httpStatus == 401 || httpStatus == 403) {
            msg = QStringLiteral("API 认证失败（%1）：API Key 无效或未授权，"
                                 "请在设置中重新填写 Key 并用「测试连接」验证").arg(httpStatus);
        } else if (httpStatus == 400 || httpStatus == 422) {
            msg = QStringLiteral("请求被拒绝（%1）：%2")
                      .arg(httpStatus)
                      .arg(serverMsg.isEmpty() ? reply->errorString() : serverMsg);
        } else {
            msg = serverMsg.isEmpty()
                      ? reply->errorString()
                      : QStringLiteral("服务端返回 %1：%2").arg(httpStatus).arg(serverMsg);
        }
        emit translateFinished(false, QString(), msg);
        return;
    }

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (perr.error != QJsonParseError::NoError) {
        emit translateFinished(false, QString(), "响应解析失败: " + perr.errorString());
        return;
    }

    QString result = doc.object()
                         .value("choices").toArray()
                         .at(0).toObject()
                         .value("message").toObject()
                         .value("content").toString();

    // 兜底清洗：部分模型（如 Qwen3 经第三方中转）会把思维链以 <think>…</think>
    // 混在 content 里输出，取闭合标签之后的正文；只有未闭合思维链则视为空结果
    const int thinkEnd = result.lastIndexOf(QStringLiteral("</think>"));
    if (thinkEnd != -1)
        result = result.mid(thinkEnd + 8);
    else if (result.contains(QStringLiteral("<think>")))
        result.clear();

    result = result.trimmed();
    if (result.isEmpty())
        emit translateFinished(false, QString(), "API 返回空结果");
    else
        emit translateFinished(true, result, QString());
}

} // namespace snipaste
