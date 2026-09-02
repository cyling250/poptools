#pragma once

#include <QObject>
#include <QString>
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
    void translate(const QString &text, const QString &fromLang, const QString &toLang,
                   bool isOcr = true);

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
                        bool isOcr, bool disableThinking);

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    bool m_busy = false;

    // 待翻译参数（降级重发时复用）
    QString m_pendingText;
    QString m_pendingFrom;
    QString m_pendingTo;
    bool m_pendingIsOcr = false;
    bool m_tryDisableThinking = true;
};

} // namespace poptools
