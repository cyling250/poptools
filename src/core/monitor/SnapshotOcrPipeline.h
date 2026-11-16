#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include "../ocr/RapidOcrService.h" // OcrBox/QVector 完整类型（moc 签名需要）
#include "UiaTextReader.h"          // Result（moc 签名需要）

class QTimer;

namespace poptools {

class RapidOcrService;

// ============================================================
// 中文上屏取文管线（T-047 升级为 UIA-first，用户对齐方案）：
//   上屏信号 → 防抖 800ms → UIA 精确取文（worker 线程）
//   → 失败/空/换窗 → 截前台窗口（内存）→ 本地 OCR 兜底
//   → 与该窗口上次文本做增量 diff → 只记新增文字
//   → 图片即弃（RapidOcrService 的临时文件自清理，全程无持久截图）。
//
// 双基线：UIA 与 OCR 的文本粒度不同（前者是光标附近精确文本，后者是
// 整屏版面文本），混用基线会产生假增量——m_lastUiaText / m_lastText
// 各自独立做增量 diff，事件行以 source 区分（"uia"/"ocr"）。
//
// 线程纪律：截图必须 GUI 线程（QScreen::grabWindow）；裁剪后的
// 缩放挪到 QThreadPool（红线1：大图缩放禁入主线程）；OCR 在
// RapidOcrService 子进程（异步 stdout）；UIA COM 调用在
// UiaTextReader 专用 worker 线程（禁入 GUI 线程）；diff 与落盘
// 回调都在主线程。
//
// 成本护栏：密码框聚焦时不截图（UIA IsPassword 判定整链静默并回写
// PasswordFieldGuard 共享缓存）；UIA 忙/OCR 忙/失败退避时限流（≥1.2s）；
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

    // UIA 滑动窗口 diff（纯函数，单测覆盖）：UIA 取文窗口是
    // [caret-2000, caret]，光标推进时窗口整体右移，旧文本既非新文本
    // 前缀/后缀也不再整体包含——先走 diffAdded 常规三案，都未命中时
    // 找"旧文本的最长后缀 == 新文本的前缀"（两窗口重叠段），余下即新增
    static QString diffUiaAdded(const QString &oldText, const QString &newText);

    // 基线消费（R6 测试补齐轮自 onOcrFinished 抽出，行为不变；纯函数，
    // 单测覆盖）：全文与 baseline[hwnd] 基线做增量 → 新增文本尾部
    // 300 字符截断（只保留最新输入）→ 基线照常推进（无新增也推进）→
    // 基线缓存超 32 窗整体清空（防膨胀）。归属门卫与失败退避仍留在槽内。
    // diff 策略可注入：OCR 路径用 diffAdded（默认），UIA 路径用
    // diffUiaAdded——双基线隔离由调用方传入各自的 QHash 实现
    typedef QString (*DiffFn)(const QString &, const QString &);
    static QString consumeBaselineAdded(QHash<quintptr, QString> &baseline,
                                        quintptr hwnd, const QString &text,
                                        DiffFn diff = &SnapshotOcrPipeline::diffAdded);

public slots:
    // 上屏信号入口（ImeCommitDetector::commitDetected）：防抖合并
    void onCommitDetected();

signals:
    // 本窗口新出现的文字（已增量去重；可能为多次上屏的合并结果）。
    // source："uia"（UIA 精确取文）/"ocr"（截图+OCR 兜底）
    void ocrTextReady(const QString &app, const QString &title, const QString &text,
                      const QString &source);

private slots:
    // 防抖到点：busy/退避/限流/密码四道门卫后发起 UIA-first 取文（计费限流预算）
    void captureNow();
    // UIA 回包落点：密码态缓存回写+整链静默；窗口/进程对号成功走 UIA 基线
    // diff，否则回落 OCR
    void onUiaFinished(const UiaTextReader::Result &result);
    // OCR 回包落点：m_pending 归属门卫 → 失败退避 5s → 版面排序拼文 →
    // OCR 基线 diff 落信号
    void onOcrFinished(bool ok, const QVector<OcrBox> &boxes, const QString &error);

private:
    // 取文尝试下半场：截图+OCR 兜底（UIA 失败/不可用/换窗后回落）。
    // 以 m_req* 为对号上下文；前台窗口已变化则本次作废
    void startOcrCapture();

    RapidOcrService *m_ocr = nullptr;
    QTimer *m_debounce = nullptr; // 800ms 单发：连打时的上屏合并
    UiaTextReader *m_uia = nullptr; // UIA-first 取文（含专用 worker 线程）

    quintptr m_hwnd = 0;
    QString m_app, m_title;

    bool m_uiaBusy = false;        // UIA 读在途（busy 门卫：新触发直接跳过）
    bool m_pending = false;        // 已有 OCR 请求在途
    quintptr m_reqHwnd = 0;        // 取文时的窗口上下文（回包对号）
    quintptr m_reqPid = 0;         // T-047-FIX(P3-1)：请求窗口进程号（UIA 回包
                                   // elPid 对号；topHwnd 不可知时的权威校验）
    QString m_reqApp, m_reqTitle;
    QHash<quintptr, QString> m_lastText;     // OCR 全文基线（按窗）
    QHash<quintptr, QString> m_lastUiaText;  // UIA 文本基线（按窗，独立于 OCR）
    qint64 m_lastCaptureMs = 0;
    qint64 m_skipUntilMs = 0;      // 失败退避：此前不截图
};

} // namespace poptools
