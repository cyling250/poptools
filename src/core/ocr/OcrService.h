#pragma once

#include <QObject>
#include <QString>
#include <QImage>
#include <QRect>
#include <QVector>
#include <QFuture>
#include <memory>
#include <atomic>

namespace snipaste {

// OCR 文本框（行级，坐标为输入图像像素）
struct OcrBox
{
    QRect rect;      // 文本框包围盒（图像像素坐标）
    QString text;    // 行文本（连成一段的连续文字）
    qreal conf = 0;  // 平均置信度 0-100
};

struct TessApi; // 引擎封装（DLL 函数指针表），.cpp 中定义

// OCR 服务：进程内加载 libtesseract-5.dll（LoadLibrary）识别图片文字（本地离线）。
// 识别在后台线程执行，完成后经信号回主线程。
// DLL 在首次 OCR 时加载，之后一直持有，不再重复 LoadLibrary。
class OcrService : public QObject
{
    Q_OBJECT
public:
    explicit OcrService(QObject *parent = nullptr);
    ~OcrService() override;

    // 识别图片中的文字（异步），完成后发 ocrFinished
    void recognize(const QImage &image);

    // 识别图片中的文字并返回文本框（异步），完成后发 ocrBoxesFinished
    void recognizeBoxes(const QImage &image);

    // 是否正忙（上一次识别未结束）
    bool busy() const { return m_busy.load(std::memory_order_relaxed); }

    // 引擎是否可用（DLL 与语言包是否齐备）
    bool available() const;

signals:
    void ocrFinished(bool ok, const QString &text, const QString &error);
    void ocrBoxesFinished(bool ok, const QVector<OcrBox> &boxes, const QString &error);

private:
    // 后台线程执行一次识别（text 模式输出纯文本；boxes 模式输出行级文本框）
    void runInBackground(const QImage &image, bool boxes);

    // 确保引擎已加载：首次调用时 LoadLibrary，之后直接返回已有指针
    TessApi *ensureLoaded();

    std::atomic<bool> m_busy{false};
    QFuture<void> m_future; // 保存后台任务，析构时等待结束，避免悬垂访问
    std::unique_ptr<TessApi> m_tess; // 持久的 DLL 函数指针表（首次 OCR 时加载）
};

} // namespace snipaste
