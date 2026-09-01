#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace snipaste {

// 去掉用户填写的 Base URL 首尾空白与尾部斜杠（常有人粘贴 https://host/v1/ 这类
// 带尾斜杠的地址），便于直接拼接 /chat/completions
inline QString normalizeApiBaseUrl(QString url)
{
    url = url.trimmed();
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url;
}

// 翻译服务：仅支持大模型 API（OpenAI 兼容接口，默认 DeepSeek）
// 单问答形式发送（无上下文），提示词说明文本来源（OCR 截图或用户划词），
// 并注入用户在设置中配置的翻译领域与专有名词术语表。
// 翻译为轻量任务，不开启模型思考：请求显式携带 thinking 禁用字段（DeepSeek 等），
// 端点不支持时自动降级；并对混入正文的 <think> 思维链做兜底清洗，保证结果直出且快。
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

} // namespace snipaste
