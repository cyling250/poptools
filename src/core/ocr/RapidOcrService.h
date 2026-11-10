#pragma once

#include <QObject>
#include <QString>
#include <QImage>
#include <QRect>
#include <QVector>
#include <QProcess>
#include <QByteArray>
#include <QTimer>

#include <algorithm>

namespace poptools {

// OCR 文本框行（复用旧接口类型，保持与 OcrController 的兼容）
struct OcrBox
{
    QRect rect;      // 文本包围盒（图像像素坐标）
    QString text;    // 行文本
    qreal conf = 0;  // 置信度 0-100
};

// 按阅读顺序重排 OCR 文本框（先上后下，同一行内先左后右）。
// 同行判定用"半行高容差"：与行首框 top 相差不超过两框行高中点视为同一行——
// 引擎对同一文本行的多个框 y 常有数像素微差，严格相等比较会错误拆行。
// 共享小函数（R3 #8）：OcrController（截图翻译）与 PinModule（贴图 OCR）
// 两处行序拼接统一调用，不再各写一份。实现为"按 top 排序后逐行分组"而非
// 两两比较谓词——带容差的比较谓词不满足严格弱序，std::sort 属未定义行为
inline void sortOcrBoxesReadingOrder(QVector<OcrBox> &boxes)
{
    std::sort(boxes.begin(), boxes.end(), [](const OcrBox &a, const OcrBox &b) {
        if (a.rect.top() != b.rect.top())
            return a.rect.top() < b.rect.top();
        return a.rect.left() < b.rect.left();
    });
    int lineStart = 0;
    while (lineStart < boxes.size())
    {
        const int anchorTop = boxes[lineStart].rect.top();
        const int anchorH = qMax(1, boxes[lineStart].rect.height());
        int lineEnd = lineStart + 1; // 行区间 [lineStart, lineEnd)
        while (lineEnd < boxes.size())
        {
            const int h = qMax(1, boxes[lineEnd].rect.height());
            if (qAbs(boxes[lineEnd].rect.top() - anchorTop)
                > qMax(anchorH, h) / 2)
                break; // 与行首框超出半行高容差：新行
            ++lineEnd;
        }
        if (lineEnd - lineStart > 1)
        {
            std::sort(boxes.begin() + lineStart, boxes.begin() + lineEnd,
                      [](const OcrBox &a, const OcrBox &b) {
                          return a.rect.left() < b.rect.left();
                      });
        }
        lineStart = lineEnd;
    }
}

// 基于 RapidOCR-json（onnxruntime，hiroi-sora）的 OCR 服务（QProcess 子进程调用）。
//
// 启动 RapidOCR-json.exe 在管道模式运行（常驻进程），启动参数固定简中配置
// （det/cls/rec + 字典，与原 PaddleOCR-json 默认行为对齐）；
// 通过 stdin 发送图片路径 JSON，从 stdout 读取结果（协议与 PaddleOCR-json 同构：
// {code:100,data:[{box,score,text}]}，101=无文字）。
// 静态单文件引擎（16.8MB，零 DLL 依赖）；请求带 20s 超时看门狗（引擎无响应时
// kill 子进程并报错，下次请求自动重启），失败路径统一发 ocrBoxesFinished；
// 空闲 3 分钟自动回收子进程（常驻内存大户，见 finishRequest）。
class RapidOcrService : public QObject
{
    Q_OBJECT
public:
    explicit RapidOcrService(QObject *parent = nullptr);
    ~RapidOcrService() override;

    // 识别图片中的文字并返回文本框（异步），完成后发 ocrBoxesFinished
    void recognizeBoxes(const QImage &image);

    // 是否正忙（上一次识别未结束）
    bool busy() const { return m_busy; }

    // 引擎是否可用（RapidOCR-json.exe 是否存在）
    bool available() const;

signals:
    void ocrBoxesFinished(bool ok, const QVector<OcrBox> &boxes, const QString &error);

private:
    // 异步启动子进程（started 信号到达后 writeCommand；启动失败经
    // errorOccurred → fail）。Running/Starting 在途时早退复用：Running 的
    // 命令由 onTempImageReady 直接补发（started 只发一次，C-N1）
    void startEngine();

    // worker 落盘完成回主线程：Running 进程直接补发命令（暖引擎），
    // Starting/NotRunning 走 startEngine（冷启动 → started 后发送），失败统一报错
    void onTempImageReady(bool ok);

    // 通过 stdin 发送命令（临时图片路径已在 m_tempFile）
    void writeCommand();

    // 处理 stdout 数据（按行分割 JSON）
    void onReadyRead();

    // 请求收尾（不含 emit）：停超时看门狗、清临时文件、复位忙态
    void finishRequest();

    // 失败收尾：finishRequest + 发失败信号
    void fail(const QString &msg);

    // 清理本次临时文件
    void cleanupTemp();

    // 引擎目录（exe 同目录下的 rapidocr/）
    static QString engineDir();

    QProcess *m_process = nullptr;
    QTimer *m_timeoutTimer = nullptr; // 识别超时看门狗（引擎无响应 → kill + 报错）
    QTimer *m_idleTimer = nullptr;    // 空闲自动回收：N 分钟无请求则结束子进程
                                      // （常驻约 770MB，是全项目最大单一开销；
                                      //  冷启动约 0.6s，回收代价可接受）
    bool m_busy = false;
    QByteArray m_buffer;      // stdout 累积缓冲区
    QString m_tempFile;       // 当前临时图片路径
};

} // namespace poptools