#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>

// 翻译服务：本地引擎或大模型 API（OpenAI 兼容接口，用户可配置 base_url/api_key/model）
class Translator : public QObject
{
    Q_OBJECT
public:
    explicit Translator(QObject *parent = nullptr);

    // 翻译文本（异步），完成后发 translateFinished
    void translate(const QString &text);

    // 是否正忙
    bool busy() const { return m_busy; }

signals:
    void translateFinished(bool ok, const QString &result, const QString &error);

private slots:
    void onReplyFinished();

private:
    // 本地引擎（预留：可扩展为词典/本地模型）
    bool translateLocal(const QString &text, QString &out);

    // 大模型 API（OpenAI 兼容 /chat/completions）
    void translateByApi(const QString &text);

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    bool m_busy = false;
};
