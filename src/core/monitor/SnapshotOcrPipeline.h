#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include "../ocr/RapidOcrService.h" // OcrBox/QVector 完整类型（moc 签名需要）

class QTimer;

namespace poptools {

class RapidOcrService;

// ============================================================
// 中文上屏 OCR 快照管线（用户对齐方案）：
//   上屏信号 → 防抖 800ms → 截前台窗口（内存）→ 本地 OCR
//   → 与该窗口上次文本做增量 diff → 只记新增文字
//   → 图片即弃（RapidOcrService 的临时文件自清理，全程无持久截图）。
//
// 线程纪律：截图必须 GUI 线程（QScreen::grabWindow）；裁剪后的
// 缩放挪到 QThreadPool（红线1：大图缩放禁入主线程）；OCR 在
// RapidOcrService 子进程（异步 stdout）；diff 与落盘回调都在主线程。
//
// 成本护栏：密码框聚焦时不截图；OCR 忙/失败退避时限流（≥1.2s）；
// 忙时直接丢弃本次（下次上屏自然补拍）；同窗文本缓存超 32 条清空。
// 归属防护（红线3）：共享 OCR 服务的回包以 m_pending 门卫校验——
// 无在途请求时的回包属于其他消费者（贴图/截图），直接丢弃。
// ============================================================
class SnapshotOcrPipeline : public QObject
{
    Q_OBJECT
public:
    explicit SnapshotOcrPipeline(RapidOcrService *ocr, QObject *parent = nullptr);
    ~SnapshotOcrPipeline() override;

    // 前台窗口上下文（切窗时更新；同窗标题变化只更新标题）
    void setWindowContext(quintptr hwnd, const QString &app, const QString &title);

    // 增量 diff：返回 newText 相对 oldText 的新增部分（纯函数，单测覆盖）
    static QString diffAdded(const QString &oldText, const QString &newText);

    // 基线消费（R6 测试补齐轮自 onOcrFinished 抽出，行为不变；纯函数，
    // 单测覆盖）：OCR 全文与 baseline[hwnd] 基线做增量 → 新增文本尾部
    // 300 字符截断（只保留最新输入）→ 基线照常推进（无新增也推进）→
    // 基线缓存超 32 窗整体清空（防膨胀）。归属门卫与失败退避仍留在槽内
    static QString consumeBaselineAdded(QHash<quintptr, QString> &baseline,
                                        quintptr hwnd, const QString &text);

public slots:
    // 上屏信号入口（ImeCommitDetector::commitDetected）：防抖合并
    void onCommitDetected();

signals:
    // 本窗口新出现的文字（已增量去重；可能为多次上屏的合并结果）
    void ocrTextReady(const QString &app, const QString &title, const QString &text);

private slots:
    void captureNow();
    void onOcrFinished(bool ok, const QVector<OcrBox> &boxes, const QString &error);

private:
    RapidOcrService *m_ocr = nullptr;
    QTimer *m_debounce = nullptr; // 800ms 单发：连打时的上屏合并

    quintptr m_hwnd = 0;
    QString m_app, m_title;

    bool m_pending = false;        // 已有 OCR 请求在途
    quintptr m_reqHwnd = 0;        // 截图时的窗口上下文（OCR 回来对号）
    QString m_reqApp, m_reqTitle;
    QHash<quintptr, QString> m_lastText; // 同窗口上次全量 OCR 文本（diff 基线）
    qint64 m_lastCaptureMs = 0;
    qint64 m_skipUntilMs = 0;      // 失败退避：此前不截图
};

} // namespace poptools
