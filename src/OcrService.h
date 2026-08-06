#pragma once

#include <QObject>
#include <QString>
#include <QImage>
#include <QProcess>

// OCR 服务：调用 Tesseract 命令行识别图片文字（本地离线）
class OcrService : public QObject
{
    Q_OBJECT
public:
    explicit OcrService(QObject *parent = nullptr);

    // 识别图片中的文字（异步），完成后发 ocrFinished
    void recognize(const QImage &image);

    // 是否正忙
    bool busy() const { return m_process.state() != QProcess::NotRunning; }

    // 引擎是否可用（tesseract.exe 是否存在）
    bool available() const;

signals:
    void ocrFinished(bool ok, const QString &text, const QString &error);

private slots:
    void onFinished(int exitCode, QProcess::ExitStatus status);

private:
    QProcess m_process;
    QImage m_lastImage;
};
