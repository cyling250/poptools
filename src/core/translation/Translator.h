#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace snipaste {

// 翻译服务：仅支持大模型 API（OpenAI 兼容接口，默认 DeepSeek）
// 单问答形式发送（无上下文），提示词说明文本来源为 OCR 截图提取。
class Translator : public QObject
{
    Q_OBJECT
public:
    explicit Translator(QObject *parent = nullptr);

    // 翻译文本（异步），完成后发 translateFinished。
    // fromLang / toLang 为语言代码（"auto" 表示源语言由模型自动识别）。
    void translate(const QString &text, const QString &fromLang, const QString &toLang);

    // 是否正忙
    bool busy() const { return m_busy; }

signals:
    void translateFinished(bool ok, const QString &result, const QString &error);

private slots:
    void onReplyFinished();

private:
    void translateByApi(const QString &text, const QString &fromLang, const QString &toLang);

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    bool m_busy = false;
};

} // namespace snipaste
