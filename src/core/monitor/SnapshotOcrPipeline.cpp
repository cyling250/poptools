#include "SnapshotOcrPipeline.h"

#include <QDateTime>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <algorithm>

#include "PasswordFieldGuard.h"
#include "../capture/ScreenCapturer.h"
#include "../ocr/RapidOcrService.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace poptools {

namespace {
constexpr int kDebounceMs = 800;     // 连打合并：最后一次上屏后 800ms 才拍
constexpr int kMinCaptureGapMs = 1200; // 限流：两次截图最小间隔
constexpr int kBackoffMs = 5000;     // OCR 失败退避
constexpr int kMaxAddedChars = 300;  // 单次事件最大新增字符数
constexpr int kMaxScaleWidth = 1600; // OCR 输入宽度上限（提速，小字仍可辨）
constexpr int kMaxWindowCache = 32;  // 同窗基线缓存上限
} // namespace

SnapshotOcrPipeline::SnapshotOcrPipeline(RapidOcrService *ocr, QObject *parent)
    : QObject(parent)
    , m_ocr(ocr)
{
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &SnapshotOcrPipeline::captureNow);
    connect(m_ocr, &RapidOcrService::ocrBoxesFinished, this,
            &SnapshotOcrPipeline::onOcrFinished);
}

SnapshotOcrPipeline::~SnapshotOcrPipeline()
{
    m_debounce->stop();
}

void SnapshotOcrPipeline::setWindowContext(quintptr hwnd, const QString &app,
                                           const QString &title)
{
    if (hwnd == m_hwnd) {
        m_title = title; // 同窗标题变化只更新（标签页/文档标题）
        return;
    }
    m_hwnd = hwnd;
    m_app = app;
    m_title = title;
}

void SnapshotOcrPipeline::onCommitDetected()
{
    // 连续上屏（逐短语提交）只保留最后一次：防抖重启即合并
    m_debounce->start();
}

void SnapshotOcrPipeline::captureNow()
{
    if (m_pending || !m_ocr || !m_ocr->available())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now < m_skipUntilMs)
        return; // 上次 OCR 失败：退避中
    if (now - m_lastCaptureMs < kMinCaptureGapMs) {
        m_debounce->start(400); // 限流：稍后重试一次
        return;
    }
    if (PasswordFieldGuard::isPasswordFocused())
        return; // 屏上有密码框：绝不截屏（OCR 文本会带出屏幕内容）

#ifdef Q_OS_WIN
    const HWND fg = GetForegroundWindow();
    if (!fg)
        return;
    // 前台窗口物理矩形：DWM 扩展边界（不含阴影；返回物理像素）
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(fg, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        GetWindowRect(fg, &r);
    const QRect winPhys(r.left, r.top, r.right - r.left, r.bottom - r.top);
    if (winPhys.isEmpty())
        return;

    const CaptureResult cap = ScreenCapturer::capture();
    const QRect crop = winPhys.intersected(cap.physDesktop);
    if (crop.isEmpty())
        return;
    m_lastCaptureMs = now;
    m_reqHwnd = reinterpret_cast<quintptr>(fg);
    m_reqApp = m_app;
    m_reqTitle = m_title;
    m_pending = true; // 在途期间新请求一律丢弃（下次上屏自然补拍）

    // QPixmap→QImage 必须在 GUI 线程；裁剪与缩放（大图操作）挪线程池。
    // 外层 lambda 以 QPointer 捕获：stopMonitor 直接 delete 管线时在途
    // 任务不得访问悬垂对象（内层排队调用以 self 为接收者，销毁后自动
    // 丢弃投递）
    QImage full = cap.screen.toImage();
    const QRect rel = crop.translated(-cap.physDesktop.topLeft());
    QPointer<SnapshotOcrPipeline> self(this);
    QThreadPool::globalInstance()->start([self, full, rel] {
        QImage part = full.copy(rel); // 隐式共享深拷贝，主线程已不再写它
        if (part.width() > kMaxScaleWidth)
            part = part.scaledToWidth(kMaxScaleWidth, Qt::FastTransformation);
        if (!self)
            return; // 管线已销毁：结果作废（DailyAggregator 同款 QPointer 交付模式）
        QMetaObject::invokeMethod(self.data(), [self, part] {
            if (!self)
                return;
            if (!self->m_ocr || self->m_ocr->busy()) {
                self->m_pending = false; // 引擎忙：丢弃本次
                return;
            }
            self->m_ocr->recognizeBoxes(part);
        }, Qt::QueuedConnection);
    });
#endif
}

void SnapshotOcrPipeline::onOcrFinished(bool ok, const QVector<OcrBox> &boxes,
                                        const QString &error)
{
    Q_UNUSED(error);
    // 归属防护（红线3，对照 OcrController::m_ocrPending 先例）：AppServices.ocr
    // 是多消费者共享单例（贴图/截图也在用）——无在途请求时的回包属于
    // 其他消费者，直接丢弃。此前会把外部文本与陈旧基线做 diff、以监控
    // 窗口名义写进 ocrtext.jsonl，并污染基线导致下次真增量丢失
    if (!m_pending)
        return;
    m_pending = false;
    if (!ok) {
        m_skipUntilMs = QDateTime::currentMSecsSinceEpoch() + kBackoffMs;
        return;
    }
    // 行序：OCR 引擎基本按版面序返回，仍按 (top,left) 排序保底
    QVector<OcrBox> sorted = boxes;
    std::sort(sorted.begin(), sorted.end(), [](const OcrBox &a, const OcrBox &b) {
        if (a.rect.top() != b.rect.top())
            return a.rect.top() < b.rect.top();
        return a.rect.left() < b.rect.left();
    });
    QStringList lines;
    for (const OcrBox &b : sorted) {
        const QString t = b.text.trimmed();
        if (!t.isEmpty())
            lines << t;
    }
    const QString text = lines.join(QLatin1Char('\n'));

    QString added = diffAdded(m_lastText.value(m_reqHwnd), text).trimmed();
    if (added.size() > kMaxAddedChars)
        added = added.right(kMaxAddedChars); // 只保留尾部（最新输入）
    m_lastText.insert(m_reqHwnd, text); // 基线更新与新增无关（无新增也要推进）
    if (m_lastText.size() > kMaxWindowCache)
        m_lastText.clear(); // 简单防膨胀：窗口集合本就高频更替
    if (!added.isEmpty())
        emit ocrTextReady(m_reqApp, m_reqTitle, added);
}

QString SnapshotOcrPipeline::diffAdded(const QString &oldText, const QString &newText)
{
    if (oldText.isEmpty())
        return newText;
    if (newText == oldText)
        return QString();
    // 版面增长型（打字）：旧文本是前缀 → 新增在尾部
    if (newText.startsWith(oldText))
        return newText.mid(oldText.size());
    // 屏幕滚动型：旧文本仍在尾部 → 新增在头部
    if (newText.endsWith(oldText))
        return newText.left(newText.size() - oldText.size());
    // 中段插入型：旧文本整体仍存在 → 取其后段
    const int idx = newText.indexOf(oldText);
    if (idx >= 0)
        return newText.mid(idx + oldText.size());
    // 版面大改（换页/弹窗）：无法定位增量，宁缺毋滥返回空（基线照常推进，
    // 避免整屏文本反复入库）
    return QString();
}

} // namespace poptools
