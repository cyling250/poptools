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
constexpr int kMinCaptureGapMs = 1200; // 限流：两次取文最小间隔
constexpr int kBackoffMs = 5000;     // OCR 失败退避
constexpr int kMaxAddedChars = 300;  // 单次事件最大新增字符数
constexpr int kMaxScaleWidth = 1600; // OCR 输入宽度上限（提速，小字仍可辨）
constexpr int kMaxWindowCache = 32;  // 同窗基线缓存上限
} // namespace

SnapshotOcrPipeline::SnapshotOcrPipeline(RapidOcrService *ocr, QObject *parent)
    : QObject(parent)
    , m_ocr(ocr)
    , m_uia(new UiaTextReader(this)) // 随管线生死（worker 线程内部自管理）
{
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &SnapshotOcrPipeline::captureNow);
    connect(m_uia, &UiaTextReader::readFinished, this,
            &SnapshotOcrPipeline::onUiaFinished);
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
    // UIA 读在途：新触发直接跳过（防堆积；UIA COM 调用无超时 API，
    // 靠 worker 独立 + busy 门卫保证 GUI 线程不受影响）
    if (m_uiaBusy || m_pending || !m_ocr || !m_ocr->available())
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
    m_lastCaptureMs = now; // 取文尝试计费（UIA 与 OCR 共用同一限流预算）
    m_reqHwnd = reinterpret_cast<quintptr>(fg);
    // T-047-FIX(P3-1)：请求窗口所属进程号——UIA 回包的 elPid 对号依据
    //（浏览器/Electron 场景 topHwnd 常不可知，见 onUiaFinished）
    DWORD reqPid = 0;
    GetWindowThreadProcessId(fg, &reqPid);
    m_reqPid = reqPid;
    m_reqApp = m_app;
    m_reqTitle = m_title;
    if (m_uia && !IsHungAppWindow(fg)) {
        // T-047-UIA：UIA-first——目标窗口未挂起才走（挂起窗口的跨进程
        // UIA 调用会被其阻塞，直接进 OCR 截图路径）
        m_uiaBusy = true;
        m_uia->readAsync(m_reqHwnd); // worker 读焦点元素；回包进 onUiaFinished
        return;
    }
    startOcrCapture();
#endif
}

void SnapshotOcrPipeline::onUiaFinished(const UiaTextReader::Result &result)
{
    m_uiaBusy = false;
    if (result.password) {
        // UIA 精确判定的密码框：整链静默（不记任何流）。判定结果回写
        // PasswordFieldGuard 共享缓存——键入流打码与 OCR 门卫用同一份
        // 前台窗口密码态，两套判定不打架（元素句柄为 0 时退记顶层窗口）
        PasswordFieldGuard::cacheUiaPassword(
            result.elHwnd ? result.elHwnd : m_reqHwnd, true);
        return;
    }
    // T-047-FIX(P3-1)：接受条件放宽为 topHwnd 或 elPid 任一对号——
    // 浏览器/Electron 场景焦点元素 CurrentNativeWindowHandle 常为 0
    //（topHwnd=0 恒不等于请求窗口），原条件恒假导致即便 PID 对号成功
    // 也全额回落 OCR。防串窗语义不变：elPid（焦点元素进程号）是权威
    // 校验（worker 侧已比对请求窗口进程），topHwnd 匹配是加强条件
    if (result.ok && (result.topHwnd == m_reqHwnd || result.elPid == m_reqPid)) {
        // T-047-UIA：UIA 成功且窗口/进程对号 → 记 source:"uia"。
        // diff 用 UIA 专用基线（与 OCR 基线隔离：文本粒度不同，混用
        // 会把对方的全量文本当增量）
        const QString added = consumeBaselineAdded(
            m_lastUiaText, m_reqHwnd, result.text, &SnapshotOcrPipeline::diffUiaAdded);
        if (!added.isEmpty())
            emit ocrTextReady(m_reqApp, m_reqTitle, added, QStringLiteral("uia"));
        return;
    }
    // UIA 失败 / 取到空 / 已换窗（worker 侧进程对号失败）→ 截图+OCR 兜底，
    // 记 source:"ocr"（同一次取文尝试，限流已在入口计费，不再重查）
    startOcrCapture();
}

void SnapshotOcrPipeline::startOcrCapture()
{
    // T-047-FIX(P2-1)：OCR 回落路径补密码复查（隐私关键）。UIA-first 之前
    // captureNow 的密码预检与截图同栈执行（微秒级间隙），现在回落点与预检
    // 被 UIA COM 往返隔开（防抖+读取可达数百 ms）——间隙内焦点切进"已开
    // 显示密码"的密码框时，OCR 结果可能带出密码明文落盘。UIA 路径的密码
    // 拦截已有，这里补回落路径的兜底复查
    if (PasswordFieldGuard::isPasswordFocused())
        return;

#ifdef Q_OS_WIN
    const HWND fg = GetForegroundWindow();
    if (!fg || reinterpret_cast<quintptr>(fg) != m_reqHwnd)
        return; // 已换窗：本次取文作废（基线不能串窗口）
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
    // R6-B2: m_pending 不在此置位（原此处过早置 true）——从 captureNow 到
    // 真正 recognizeBoxes 隔着线程池任务 + 一次排队投递两跳，期间的窗口里
    // 外来 OCR 消费者（贴图/截图）的回包会经 onOcrFinished 的 m_pending
    // 门卫被误认作自己的回包：做 diff、写 ocrtext.jsonl 隐私日志并污染
    // m_lastText 基线。置位挪到排队 lambda 内 busy 检查通过之后、
    // recognizeBoxes 之前（对齐 OcrController.cpp 先例时序）；期间新请求
    // 由 kMinCaptureGapMs 限流兜底。req* 上下文留在 captureNow 赋值
    //（只在 m_pending 门卫通过后被读）

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
                // R6-B2: 引擎忙直接丢弃（m_pending 尚未置位，无需复位）
                return;
            }
            self->m_pending = true; // R6-B2: 归属标记紧贴 recognizeBoxes 置位，
            self->m_ocr->recognizeBoxes(part); // 在途回包才归本管线（门卫语义不变）
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

    const QString added = consumeBaselineAdded(m_lastText, m_reqHwnd, text);
    if (!added.isEmpty())
        emit ocrTextReady(m_reqApp, m_reqTitle, added, QStringLiteral("ocr"));
}

QString SnapshotOcrPipeline::consumeBaselineAdded(QHash<quintptr, QString> &baseline,
                                                  quintptr hwnd, const QString &text,
                                                  DiffFn diff)
{
    QString added = diff(baseline.value(hwnd), text).trimmed();
    if (added.size() > kMaxAddedChars)
        added = added.right(kMaxAddedChars); // 只保留尾部（最新输入）
    baseline.insert(hwnd, text); // 基线更新与新增无关（无新增也要推进）
    if (baseline.size() > kMaxWindowCache)
        baseline.clear(); // 简单防膨胀：窗口集合本就高频更替
    return added;
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

QString SnapshotOcrPipeline::diffUiaAdded(const QString &oldText, const QString &newText)
{
    // T-047-UIA：先走常规三案（窗口未满/首拍时的增长与滚动）
    const QString added = diffAdded(oldText, newText);
    if (!added.isEmpty())
        return added;
    if (newText.isEmpty() || newText == oldText)
        return QString();
    // UIA 滑动窗口补解：取文窗口是 [caret-2000, caret]，光标推进 k 字符
    // 后窗口整体右移 k，旧文本头部滑出、新文本尾部滑入——旧文本既非
    // 新文本的前缀/后缀也不再整体包含，diffAdded 三案全部落空。
    // 解法：旧文本的最长后缀 == 新文本的前缀（= 两窗口的重叠段），
    // 新文本余下的尾部即新增。从最长候选降序试起，首个命中即最大重叠
    // → 新增最少（宁缺毋滥；周期性文本可能少记，与 diffAdded 同哲学）
    const int maxL = qMin(oldText.size(), newText.size());
    for (int l = maxL; l > 0; --l) {
        if (oldText.right(l) == newText.left(l))
            return newText.mid(l);
    }
    return QString();
}

} // namespace poptools
