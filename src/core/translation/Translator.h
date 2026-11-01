#pragma once

#include <QObject>
#include <QString>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace poptools {

// 去掉用户填写的 Base URL 首尾空白与尾部斜杠（常有人粘贴 https://host/v1/ 这类
// 带尾斜杠的地址），便于直接拼接端点路径
inline QString normalizeApiBaseUrl(QString url)
{
    url = url.trimmed();
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url;
}

// API 接口协议格式
enum class ApiFormat { OpenAI, Anthropic };

// 设置存储值（"openai"/"anthropic"）→ 枚举，未知值回退 OpenAI
inline ApiFormat apiFormatFromSetting(const QString &value)
{
    return value == QLatin1String("anthropic") ? ApiFormat::Anthropic : ApiFormat::OpenAI;
}

// 各格式的请求端点路径（拼接在规范化 Base URL 后）：
//   OpenAI    → /chat/completions
//   Anthropic → /v1/messages。兼容服务的 Anthropic 端点常挂 …/anthropic 前缀下
//   （DeepSeek：Base URL 填 https://api.deepseek.com/anthropic；官方 Claude 填
//   https://api.anthropic.com）；base 已以 /v1 结尾时不重复追加，容错直接粘贴 …/v1
inline QString apiEndpointPath(ApiFormat format, const QString &normalizedBase)
{
    if (format == ApiFormat::Anthropic)
        return normalizedBase.endsWith(QLatin1String("/v1"))
                   ? QStringLiteral("/messages")
                   : QStringLiteral("/v1/messages");
    return QStringLiteral("/chat/completions");
}

// 按格式设置鉴权头：OpenAI 用 Bearer；Anthropic 用 x-api-key + anthropic-version
inline void applyApiAuthHeaders(QNetworkRequest &req, ApiFormat format, const QString &apiKey)
{
    if (format == ApiFormat::Anthropic) {
        req.setRawHeader("x-api-key", apiKey.toUtf8());
        req.setRawHeader("anthropic-version", "2023-06-01");
    } else {
        req.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
    }
}

// 语言代码 → 提示词中的自然语言名称（模型更易理解；划词卡片/文字栏映射共用）
inline QString langName(const QString &code)
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

// ============================================================
// 翻译卡片的 LLM 动作：除翻译外的延伸指令（提示词见 translateActionPrompt）。
// 枚举顺序 = 卡片动作下拉顺序 = llmActionNames() 顺序
// ============================================================
enum class LlmAction { Translate, Explain, Summarize, Rewrite };

// 动作显示名（卡片动作下拉，下标即 LlmAction 枚举值）
inline QStringList llmActionNames()
{
    return {QStringLiteral("翻译"), QStringLiteral("解释"),
            QStringLiteral("总结"), QStringLiteral("改写")};
}

// 动作 ↔ 持久化键（Settings.translateAction，未知值回退翻译）
inline QString llmActionKey(LlmAction action)
{
    switch (action) {
    case LlmAction::Explain:   return QStringLiteral("explain");
    case LlmAction::Summarize: return QStringLiteral("summarize");
    case LlmAction::Rewrite:   return QStringLiteral("rewrite");
    case LlmAction::Translate: break;
    }
    return QStringLiteral("translate");
}

inline LlmAction llmActionFromKey(const QString &key)
{
    if (key == QLatin1String("explain"))   return LlmAction::Explain;
    if (key == QLatin1String("summarize")) return LlmAction::Summarize;
    if (key == QLatin1String("rewrite"))   return LlmAction::Rewrite;
    return LlmAction::Translate;
}

// 按动作生成系统提示词（纯函数，Translator 与单元测试共用）。
//   Translate  翻译指令（isOcr 时额外声明文本来自截图、可能有噪声）；
//   Explain / Summarize  以目标语言输出（目标语言为 auto 时由模型按源语言判断）；
//   Rewrite    润色改写，保持原文语言输出。
// 所有动作都明确禁止输出思考过程（配合请求体 thinking 禁用字段双保险）。
inline QString translateActionPrompt(LlmAction action, const QString &fromLang,
                                     const QString &toLang, bool isOcr)
{
    switch (action) {
    case LlmAction::Explain:
        return QStringLiteral(
            "You are a knowledgeable assistant. Explain the text below in %1 — what it means, "
            "key terms, and any background needed to understand it. Be concise. "
            "Output only the explanation — no reasoning, no <think> tags.")
            .arg(langName(toLang));
    case LlmAction::Summarize:
        return QStringLiteral(
            "You are a concise assistant. Summarize the text below in %1, capturing the key "
            "points as a short paragraph or list. "
            "Output only the summary — no reasoning, no <think> tags.")
            .arg(langName(toLang));
    case LlmAction::Rewrite:
        return QStringLiteral(
            "You are a professional editor. Rewrite and polish the text below, keeping its "
            "original language. Fix grammar, improve fluency and clarity while preserving "
            "meaning and tone. Output ONLY the rewritten text — no explanations, no notes, "
            "no reasoning, no <think> tags, no extra content.");
    case LlmAction::Translate:
        break;
    }
    if (isOcr) {
        return QStringLiteral(
            "You are a professional translation engine. The text below was extracted from a "
            "screenshot via OCR, so it may contain typos, missing or incorrect punctuation, or "
            "stray content such as UI labels, numbers, or code. Translate the text from %1 to %2 "
            "according to its actual context and meaning. Output ONLY the translated text — no "
            "explanations, no notes, no reasoning, no <think> tags, no extra content.")
            .arg(langName(fromLang), langName(toLang));
    }
    // 划词翻译：文本直接来自用户选中，无 OCR 噪声
    return QStringLiteral(
        "You are a professional translation engine. Translate the text below from %1 to %2. "
        "Output ONLY the translated text — no explanations, no notes, no reasoning, "
        "no <think> tags, no extra content.")
        .arg(langName(fromLang), langName(toLang));
}

// 构造翻译请求体（纯函数，Translator 与单元测试共用，双协议只此一处实现）：
//   OpenAI    → messages = [system, user]，temperature 0.2；
//   Anthropic → system 独立于 messages、messages = [user]，max_tokens 必填（8192）。
// disableThinking：请求体携带 thinking 禁用字段（DeepSeek V4 实测合法值为
//   adaptive/enabled/disabled；端点不识别报 400/422 时由调用方降级重发）
inline QByteArray buildTranslateRequestBody(ApiFormat format, const QString &model,
                                            const QString &systemPrompt,
                                            const QString &text, bool disableThinking)
{
    if (format == ApiFormat::Anthropic) {
        QJsonObject body{
            {"model", model},
            {"max_tokens", 8192},
            {"temperature", 0.2},
            {"system", systemPrompt},
            {"messages", QJsonArray{QJsonObject{{"role", "user"}, {"content", text}}}},
        };
        if (disableThinking)
            body.insert("thinking", QJsonObject{{"type", "disabled"}});
        return QJsonDocument(body).toJson();
    }
    // 单问答形式（messages 只有 system + user 两条，无历史上下文）
    QJsonObject body{
        {"model", model},
        {"messages", QJsonArray{
            QJsonObject{{"role", "system"}, {"content", systemPrompt}},
            QJsonObject{{"role", "user"}, {"content", text}},
        }},
        {"temperature", 0.2},
    };
    if (disableThinking)
        body.insert("thinking", QJsonObject{{"type", "disabled"}});
    return QJsonDocument(body).toJson();
}

// 翻译服务：仅支持大模型 API，接口格式二选一（设置 translate/apiFormat）：
//   * OpenAI 兼容（默认）：/chat/completions + Bearer，请求显式携带 thinking 禁用
//     字段，端点不支持时自动降级重发；
//   * Anthropic：/v1/messages + x-api-key，system 独立于 messages，max_tokens 必填；
//     思考模式默认关闭（extended thinking 为显式开启项），无需禁用字段。
// 单问答形式发送（无上下文），提示词说明文本来源（OCR 截图或用户划词），
// 并注入用户在设置中配置的翻译领域与专有名词术语表。
// 另对混入正文的 <think> 思维链做兜底清洗，保证结果直出且快。
class Translator : public QObject
{
    Q_OBJECT
public:
    explicit Translator(QObject *parent = nullptr);

    // 翻译文本（异步），完成后发 translateFinished。
    // fromLang / toLang 为语言代码（"auto" 表示源语言由模型自动识别）。
    // isOcr=true：文本来自 OCR 截图（可能有错字/标点缺失）；isOcr=false：文本来自用户划词。
    // action：LLM 动作（默认翻译；解释/总结/改写见 LlmAction）
    void translate(const QString &text, const QString &fromLang, const QString &toLang,
                   bool isOcr = true, LlmAction action = LlmAction::Translate);

    // 是否正忙
    bool busy() const { return m_busy; }

signals:
    void translateFinished(bool ok, const QString &result, const QString &error);

private slots:
    void onReplyFinished();

private:
    // disableThinking：请求体携带 thinking 禁用字段；服务器不识别该字段报 400/422 时
    // 由 onReplyFinished 去掉字段降级重发一次（兼容严格校验的 OpenAI 端点）
    void translateByApi(const QString &text, const QString &fromLang, const QString &toLang,
                        bool isOcr, bool disableThinking, LlmAction action);

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    bool m_busy = false;

    // 待翻译参数（降级重发时复用）
    QString m_pendingText;
    QString m_pendingFrom;
    QString m_pendingTo;
    bool m_pendingIsOcr = false;
    LlmAction m_pendingAction = LlmAction::Translate;
    bool m_tryDisableThinking = true;
};

} // namespace poptools
