#pragma once

#include <QObject>
#include <QString>
#include <QImage>
#include <QRect>
#include <QVector>
#include <QProcess>
#include <QByteArray>

namespace poptools {

// OCR 文本框行（复用旧接口类型，保持与 OcrController 的兼容）
struct OcrBox
{
    QRect rect;      // 文本包围盒（图像像素坐标）
    QString text;    // 行文本
    qreal conf = 0;  // 置信度 0-100
};

// 基于 PaddleOCR-json 的 OCR 服务（QProcess 子进程调用）。
//
// 启动 PaddleOCR-json.exe 在管道模式运行（常驻进程），
// 通过 stdin 发送图片路径，从 stdout 读取 JSON 结果。
// 模型初始化约 0.6s，后续识别约 200ms/张。
// 与旧 OcrService 保持相同接口（recognize, recognizeBoxes, signals）。
class PaddleOcrService : public QObject
{
    Q_OBJECT
public:
    explicit PaddleOcrService(QObject *parent = nullptr);
    ~PaddleOcrService() override;

    // 识别图片中的文字（异步），完成后发 ocrFinished
    void recognize(const QImage &image);

    // 识别图片中的文字并返回文本框（异步），完成后发 ocrBoxesFinished
    void recognizeBoxes(const QImage &image);

    // 是否正忙（上一次识别未结束）
    bool busy() const { return m_busy; }

    // 引擎是否可用（PaddleOCR-json.exe 是否存在）
    bool available() const;

signals:
    void ocrFinished(bool ok, const QString &text, const QString &error);
    void ocrBoxesFinished(bool ok, const QVector<OcrBox> &boxes, const QString &error);

private:
    // 确保子进程启动并初始化完成
    void ensureProcess();

    // 保存图片到临时文件，通过 stdin 发送命令
    void sendImage(const QImage &image);

    // 处理 stdout 数据（按行分割 JSON）
    void onReadyRead();

    // 清理本次临时文件
    void cleanupTemp();

    // PaddleOCR-json 目录（exe 同目录下的 paddleocr/）
    static QString paddleDir();

    QProcess *m_process = nullptr;
    bool m_busy = false;
    bool m_boxesMode = false; // 当前请求是否需要 boxes
    QByteArray m_buffer;      // stdout 累积缓冲区
    QString m_tempFile;       // 当前临时图片路径
};

} // namespace poptools