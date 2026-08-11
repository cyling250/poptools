#pragma once

#include <QObject>
#include <QString>
#include <QImage>
#include <QProcess>
#include <QRect>
#include <QVector>

namespace snipaste {

// OCR 文本框（tesseract TSV 输出，坐标为输入图像像素）
struct OcrBox
{
    QRect rect;      // 文本框包围盒（图像像素坐标）
    QString text;    // 行文本（连成一段的连续文字）
    qreal conf = 0;  // 平均置信度 0-100
};

// OCR 服务：调用 Tesseract 命令行识别图片文字（本地离线）
class OcrService : public QObject
{
    Q_OBJECT
public:
    explicit OcrService(QObject *parent = nullptr);

    // 识别图片中的文字（异步），完成后发 ocrFinished
    void recognize(const QImage &image);

    // 识别图片中的文字并返回文本框（异步），完成后发 ocrBoxesFinished
    void recognizeBoxes(const QImage &image);

    // 是否正忙
    bool busy() const { return m_process.state() != QProcess::NotRunning; }

    // 引擎是否可用（tesseract.exe 是否存在）
    bool available() const;

signals:
    void ocrFinished(bool ok, const QString &text, const QString &error);
    void ocrBoxesFinished(bool ok, const QVector<OcrBox> &boxes, const QString &error);

private slots:
    void onFinished(int exitCode, QProcess::ExitStatus status);

private:
    // 启动识别（text 模式输出纯文本；boxes 模式输出 TSV 并解析行级文本框）
    void start(const QImage &image, bool boxes);
    // 删除当前 OCR 工作目录并清空 m_workDir
    void cleanupWorkDir();

    QProcess m_process;
    QImage m_lastImage;
    bool m_boxesMode = false;
    QString m_workDir; // 当前 OCR 工作目录（软件目录下），识别结束后清理
};

} // namespace snipaste
