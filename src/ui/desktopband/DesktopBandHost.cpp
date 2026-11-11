#include "DesktopBandHost.h"

#include "BandMouseRouter.h"

#include <QApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

#include <d3d11.h>
#include <dcomp.h>
#include <dxgi.h>

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QWheelEvent> // T-048-FIX（P2-2）：滚轮派发

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

namespace poptools {
namespace desktopband {

namespace {

// T-048：复用 t048_dcomp.cpp 探针已验证的窗口规格与 DComp 链路。

// 窗口类（进程级注册一次）——结构冒烟探针按类名枚举本宿主
constexpr wchar_t kBandWndClass[] = L"PopToolsDesktopBandHostWnd";

// void* → HWND（头文件不引 windows.h，成员以 void* 存储）
inline HWND hwndOf(void *p) { return static_cast<HWND>(p); }

// band 定位失败后的快速失败冷却（毫秒）：降级路径每次 show 都会重试，
// 防止 Explorer 异常期间反复阻塞主线程重探
constexpr qint64 kBandFailCooldownMs = 10000;

// 自纠定时周期（毫秒）：低频检查带归属与带内 Z 序
constexpr int kHealIntervalMs = 5000;

// 带窗过程：WM_NCCREATE 时把 DesktopBandHost 指针挂到 GWLP_USERDATA，
// 其余消息一律转回宿主成员处理
LRESULT CALLBACK bandWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
        SetWindowLongPtrW(h, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(h, msg, wp, lp);
    }
    auto *host = reinterpret_cast<DesktopBandHost *>(
        GetWindowLongPtrW(h, GWLP_USERDATA));
    if (host)
        return static_cast<LRESULT>(
            host->nativeMessage(int(msg), quintptr(wp), qintptr(lp)));
    return DefWindowProcW(h, msg, wp, lp);
}

bool ensureBandWindowClass()
{
    static const bool done = [] {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = bandWndProc;
        wc.lpszClassName = kBandWndClass;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        return RegisterClassExW(&wc) != 0
               || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();
    return done;
}

// 顶层 WorkerW 收集（图标层缺席时的带判定输入）
struct WorkerScan
{
    QVector<BandProbeInput::TopWorker> list;
};
BOOL CALLBACK collectWorker(HWND hwnd, LPARAM lp)
{
    wchar_t cls[32] = {};
    if (GetClassNameW(hwnd, cls, 32) && wcscmp(cls, L"WorkerW") == 0) {
        auto *scan = reinterpret_cast<WorkerScan *>(lp);
        BandProbeInput::TopWorker w;
        w.hwnd = hwnd;
        w.hasDefViewChild =
            FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr;
        scan->list.append(w);
    }
    return TRUE;
}

void collectProbeInput(HWND progman, BandProbeInput &in)
{
    in.progman = progman;
    in.defview = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
    in.workerAfterDefview =
        in.defview ? FindWindowExW(progman, static_cast<HWND>(const_cast<void *>(in.defview)),
                                   L"WorkerW", nullptr)
                   : nullptr;
    in.topWorkers.clear();
    WorkerScan scan;
    EnumWindows(collectWorker, reinterpret_cast<LPARAM>(&scan));
    in.topWorkers = scan.list;
}

// band 定位（懒执行，首次驻入时调用）：注入式决策 resolveBand +
// 0x052C 幂等触发重探。近期失败进入快速失败冷却。
HWND locateBandBlocking()
{
    static qint64 s_lastFailMs = -kBandFailCooldownMs;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - s_lastFailMs < kBandFailCooldownMs)
        return nullptr;
    const HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) {
        s_lastFailMs = now;
        return nullptr;
    }
    // T-048-FIX（P2-3）：探测上限 6×(1000ms+80ms)=6.5s 阻塞 GUI 线程过久
    // ——降为 3×(200ms+80ms)≈0.9s（最坏），冷却机制不动
    for (int attempt = 0; attempt < 3; ++attempt) {
        BandProbeInput in;
        collectProbeInput(progman, in);
        const BandProbeResult r = resolveBand(in);
        if (r.verdict == BandVerdict::Ready)
            return static_cast<HWND>(const_cast<void *>(r.band));
        if (r.verdict == BandVerdict::Fail)
            break;
        // Splice：0x052C 幂等触发分裂（本机分裂已存在时为无操作）
        SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 200, nullptr);
        Sleep(80);
    }
    s_lastFailMs = now;
    return nullptr;
}

Qt::KeyboardModifiers currentKeyModifiers()
{
    Qt::KeyboardModifiers mods;
    if (GetKeyState(VK_SHIFT) < 0)
        mods |= Qt::ShiftModifier;
    if (GetKeyState(VK_CONTROL) < 0)
        mods |= Qt::ControlModifier;
    if (GetKeyState(VK_MENU) < 0)
        mods |= Qt::AltModifier;
    if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0)
        mods |= Qt::MetaModifier;
    return mods;
}

Qt::MouseButtons buttonsFromWp(quintptr wp)
{
    Qt::MouseButtons buttons;
    if (wp & MK_LBUTTON)
        buttons |= Qt::LeftButton;
    if (wp & MK_RBUTTON)
        buttons |= Qt::RightButton;
    if (wp & MK_MBUTTON)
        buttons |= Qt::MiddleButton;
    return buttons;
}

} // namespace

// ============================================================
// DesktopBandHost
// ============================================================

DesktopBandHost::DesktopBandHost(QObject *parent)
    : QObject(parent)
{
}

DesktopBandHost::~DesktopBandHost()
{
    deactivate();
}

// ---------- 驻入 / 退出 ----------

bool DesktopBandHost::activate(const QRect &logicalRect, const QString &title,
                               BandLayer layer)
{
    m_layer = layer;
    m_title = title;
    if (m_active) {
        // T-048-FIX（P3-6）：m_active 但宿主窗已被 Explorer 重建带销毁时
        // 不再早退返回 true（否则留下"最长 5s 带缺席且真窗口已隐藏"的
        // 空窗）——立即释放残链并原地整链重建
        if (!m_hwnd || !IsWindow(hwndOf(m_hwnd))) {
            qWarning("[band] activate 发现宿主窗已销毁——立即重建驻入");
            deactivate();
        } else {
            showBand();
            placeWindow(logicalRect);
            return true;
        }
    }

    // 1. band 定位（失败交调用方降级，不阻塞功能）
    const HWND band = locateBandBlocking();
    if (!band) {
        qWarning("[band] 壁纸带定位失败（Progman/WorkerW 不可得），交由调用方降级");
        return false;
    }
    // 2. 带规格窗口 → 3. SetParent 进带（硬前提：先驻入再建 DComp target）
    if (!createBandWindow(logicalRect, title))
        return false;
    if (!SetParent(hwndOf(m_hwnd), band)) {
        qWarning("[band] SetParent 进壁纸带失败 err=%lu", GetLastError());
        destroyBandWindow();
        return false;
    }
    m_band = band;
    refreshBandOffset();
    placeWindow(logicalRect);
    // T-048-FIX（P1-1）：activate 只建链、一律不 ShowWindow——呈现统一由
    // showBand()（内容可见性门）/ showBandInternal()（heal 恢复）负责。
    // 若在此无条件或按内容可见性 ShowWindow，heal 重建于编辑模式/打字挂起
    // 态（真窗口可见）仍会把带内容复活成双份同屏
    m_bandShown = false;
    // 4. DComp 初始化（顺序不可反）
    if (!initDComp()) {
        releaseDComp();
        destroyBandWindow();
        return false;
    }
    m_logicalRect = logicalRect;
    m_active = true;
    BandMouseRouter::instance()->addHost(this);
    if (!m_healTimer) {
        m_healTimer = new QTimer(this);
        connect(m_healTimer, &QTimer::timeout, this,
                &DesktopBandHost::checkBandHealth);
    }
    m_healTimer->start(kHealIntervalMs);
    qInfo("[band] 宿主已驻入壁纸带 band=%p rect=%dx%d layer=%d", m_band,
          m_nativeRect.toRect().width(), m_nativeRect.toRect().height(),
          int(m_layer));
    return true;
}

void DesktopBandHost::deactivate()
{
    if (m_healTimer)
        m_healTimer->stop();
    if (m_routeId >= 0) {
        // 应用退出期路由器可能已随 qApp 先行销毁：instance() 会试图新建，
        // 此处直接跳过注销（钩子随后随进程卸载，无泄漏）
        if (QCoreApplication::instance())
            BandMouseRouter::instance()->removeHost(this);
        m_routeId = -1;
    }
    releaseDComp();
    destroyBandWindow();
    m_active = false;
    m_bandShown = false;
    m_lastImage = QImage();
    m_surfaceSize = QSize();
}

void DesktopBandHost::showBand()
{
    // T-048-FIX（P1-1）：内容可见性门——内容控件不可见（真隐藏态）不显示
    // 带呈现，防降级/重建把用户已 ✕ 关闭的窗口复活。正常入口（驻带
    // glueIfFixed / 挂起恢复）均已保证调用时内容可见；heal 整链重建的
    // 呈现恢复走 showBandInternal 旁路（按重建前呈现态还原）
    if (m_content && !m_content->isVisible())
        return;
    showBandInternal();
}

void DesktopBandHost::showBandInternal()
{
    if (m_active && m_hwnd && !m_bandShown) {
        ShowWindow(hwndOf(m_hwnd), SW_SHOWNOACTIVATE);
        m_bandShown = true;
        BandMouseRouter::instance()->markRectsDirty();
    }
}

void DesktopBandHost::hideBand()
{
    if (m_active && m_hwnd && m_bandShown) {
        ShowWindow(hwndOf(m_hwnd), SW_HIDE);
        m_bandShown = false;
        BandMouseRouter::instance()->markRectsDirty();
        // 呈现中途隐藏（编辑模式/打字挂起）：清路由捕获状态防悬挂
        BandMouseRouter::instance()->resetRouteState();
    }
}

// ---------- 呈现 ----------

QImage DesktopBandHost::renderContentToImage(QWidget *content)
{
    if (!content)
        return QImage();
    const QImage img = content->grab().toImage();
    return img.format() == QImage::Format_ARGB32_Premultiplied
               ? img
               : img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

void DesktopBandHost::presentWidget(QWidget *content)
{
    if (!content)
        return;
    m_content = content;
    if (!m_active)
        return;
    syncGeometryFromWidget(content);
    presentImage(renderContentToImage(content));
}

void DesktopBandHost::presentImage(const QImage &image)
{
    if (!m_active)
        return;
    const QImage img =
        image.format() == QImage::Format_ARGB32_Premultiplied
            ? image
            : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (img.isNull())
        return;
    m_lastImage = img;
    // 上传合并（性能红线）：单次事件循环至多一次 Commit
    if (m_coalescer.submit())
        QMetaObject::invokeMethod(this, &DesktopBandHost::flushPendingImage,
                                  Qt::QueuedConnection);
}

void DesktopBandHost::flushPendingImage()
{
    if (!m_coalescer.beginFlush())
        return;
    if (!m_lastImage.isNull())
        uploadImage(m_lastImage);
}

void DesktopBandHost::syncGeometryFromWidget(QWidget *content)
{
    const QRect logical = content->geometry(); // 无框顶层：即屏幕全局几何
    if (logical != m_logicalRect)
        placeWindow(logical);
}

// ---------- 几何（逻辑 ↔ 物理映射） ----------

void DesktopBandHost::updateDprForRect(const QRect &logicalRect)
{
    QScreen *screen = QGuiApplication::screenAt(logicalRect.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    m_dpr = screen ? screen->devicePixelRatio() : 1.0;
}

QRect DesktopBandHost::nativeRectForLogical(const QRect &logicalRect) const
{
    // 逻辑全局坐标 → 物理屏幕坐标：所在屏 dpr 线性换算 + 所在显示器
    // 矩形锚定（主屏/统一缩放精确；混合 DPI 副屏为就近近似）
#ifdef Q_OS_WIN
    const POINT probe = { LONG(logicalRect.center().x() * m_dpr),
                          LONG(logicalRect.center().y() * m_dpr) };
    const HMONITOR monitor = MonitorFromPoint(probe, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    QScreen *screen = QGuiApplication::screenAt(logicalRect.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (monitor && GetMonitorInfoW(monitor, &mi) && screen) {
        const QRect scrLogical = screen->geometry();
        return QRect(int(mi.rcMonitor.left
                         + (logicalRect.left() - scrLogical.left()) * m_dpr),
                     int(mi.rcMonitor.top
                         + (logicalRect.top() - scrLogical.top()) * m_dpr),
                     int(logicalRect.width() * m_dpr),
                     int(logicalRect.height() * m_dpr));
    }
#endif
    return QRect(int(logicalRect.x() * m_dpr), int(logicalRect.y() * m_dpr),
                 int(logicalRect.width() * m_dpr),
                 int(logicalRect.height() * m_dpr));
}

void DesktopBandHost::placeWindow(const QRect &logicalRect)
{
    m_logicalRect = logicalRect;
    if (!m_hwnd)
        return;
    updateDprForRect(logicalRect);
    m_nativeRect = QRectF(nativeRectForLogical(logicalRect));
    // 子窗坐标相对带客户区：减去带客户区原点的屏幕物理坐标
    const QPointF rel = m_nativeRect.topLeft() - QPointF(m_bandOffset);
    SetWindowPos(hwndOf(m_hwnd), HWND_TOP, int(rel.x()), int(rel.y()),
                 int(m_nativeRect.width()), int(m_nativeRect.height()),
                 SWP_NOACTIVATE);
    BandMouseRouter::instance()->markRectsDirty();
}

void DesktopBandHost::refreshBandOffset()
{
    POINT origin = { 0, 0 };
    if (m_band && ClientToScreen(hwndOf(m_band), &origin))
        m_bandOffset = QPoint(origin.x, origin.y);
    else
        m_bandOffset = QPoint(0, 0);
}

// ---------- 带规格窗口 ----------

bool DesktopBandHost::createBandWindow(const QRect &logicalRect,
                                       const QString &title)
{
    if (!ensureBandWindowClass()) {
        qWarning("[band] 窗口类注册失败");
        return false;
    }
    updateDprForRect(logicalRect);
    const QRect nat = nativeRectForLogical(logicalRect);
    m_hwnd = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kBandWndClass, reinterpret_cast<const wchar_t *>(title.utf16()), WS_POPUP,
        nat.x(), nat.y(), nat.width(), nat.height(), nullptr, nullptr,
        GetModuleHandleW(nullptr), this);
    if (!m_hwnd) {
        qWarning("[band] CreateWindowEx 失败 err=%lu", GetLastError());
        return false;
    }
    m_nativeRect = QRectF(nat);
    return true;
}

void DesktopBandHost::destroyBandWindow()
{
    if (m_hwnd) {
        DestroyWindow(hwndOf(m_hwnd));
        m_hwnd = nullptr;
    }
    m_band = nullptr;
    m_bandOffset = QPoint(0, 0);
}

// ---------- DComp（探针验证链路，GDI GetDC 路径禁用） ----------

bool DesktopBandHost::initDComp()
{
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                   D3D11_SDK_VERSION, &m_d3d, nullptr, &m_ctx);
    if (FAILED(hr)) {
        // 硬件设备不可得时回退 WARP（探针同款兜底）
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                               D3D11_SDK_VERSION, &m_d3d, nullptr, &m_ctx);
    }
    if (FAILED(hr)) {
        qWarning("[band] D3D11CreateDevice 失败 hr=0x%08lX", (unsigned long)hr);
        return false;
    }
    hr = m_d3d->QueryInterface(&m_dxgi);
    if (FAILED(hr)) {
        qWarning("[band] QI IDXGIDevice 失败 hr=0x%08lX", (unsigned long)hr);
        return false;
    }
    hr = DCompositionCreateDevice(m_dxgi, __uuidof(IDCompositionDevice),
                                  reinterpret_cast<void **>(&m_dcomp));
    if (FAILED(hr)) {
        qWarning("[band] DCompositionCreateDevice 失败 hr=0x%08lX",
                 (unsigned long)hr);
        return false;
    }
    // 硬前提：此刻窗口已 SetParent 进壁纸带，CreateTargetForHwnd 才可见
    hr = m_dcomp->CreateTargetForHwnd(hwndOf(m_hwnd), FALSE, &m_target);
    if (FAILED(hr)) {
        qWarning("[band] CreateTargetForHwnd 失败 hr=0x%08lX", (unsigned long)hr);
        return false;
    }
    hr = m_dcomp->CreateVisual(&m_visual);
    if (FAILED(hr)) {
        qWarning("[band] CreateVisual 失败 hr=0x%08lX", (unsigned long)hr);
        return false;
    }
    hr = m_target->SetRoot(m_visual);
    if (FAILED(hr)) {
        qWarning("[band] Target::SetRoot 失败 hr=0x%08lX", (unsigned long)hr);
        return false;
    }
    hr = m_dcomp->Commit();
    if (FAILED(hr)) {
        qWarning("[band] 初始 Commit 失败 hr=0x%08lX", (unsigned long)hr);
        return false;
    }
    return true;
}

void DesktopBandHost::releaseDComp()
{
    // 释放顺序与探针一致：surf → visual → target → dcomp → dxgi → ctx → d3d
    if (m_surface) {
        m_surface->Release();
        m_surface = nullptr;
    }
    if (m_visual) {
        m_visual->Release();
        m_visual = nullptr;
    }
    if (m_target) {
        m_target->Release();
        m_target = nullptr;
    }
    if (m_dcomp) {
        m_dcomp->Release();
        m_dcomp = nullptr;
    }
    if (m_dxgi) {
        m_dxgi->Release();
        m_dxgi = nullptr;
    }
    if (m_ctx) {
        m_ctx->Release();
        m_ctx = nullptr;
    }
    if (m_d3d) {
        m_d3d->Release();
        m_d3d = nullptr;
    }
    m_surfaceSize = QSize();
}

bool DesktopBandHost::ensureSurface(const QSize &physSize)
{
    if (physSize.isEmpty() || !m_dcomp || !m_visual)
        return false;
    if (m_surface && m_surfaceSize == physSize)
        return true;
    IDCompositionSurface *surf = nullptr;
    // ARGB32_Premultiplied ↔ B8G8R8A8 + PREMULTIPLIED：透明圆角随带合成
    HRESULT hr = m_dcomp->CreateSurface(physSize.width(), physSize.height(),
                                        DXGI_FORMAT_B8G8R8A8_UNORM,
                                        DXGI_ALPHA_MODE_PREMULTIPLIED, &surf);
    if (FAILED(hr)) {
        qWarning("[band] CreateSurface(%dx%d) 失败 hr=0x%08lX", physSize.width(),
                 physSize.height(), (unsigned long)hr);
        return false;
    }
    hr = m_visual->SetContent(surf);
    if (FAILED(hr)) {
        surf->Release();
        qWarning("[band] Visual::SetContent 失败 hr=0x%08lX", (unsigned long)hr);
        return false;
    }
    if (m_surface)
        m_surface->Release();
    m_surface = surf;
    m_surfaceSize = physSize;
    return true;
}

void DesktopBandHost::uploadImage(const QImage &image)
{
    if (!m_active || image.isNull() || !m_hwnd)
        return;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!ensureSurface(image.size()))
            return;
        POINT offset = { 0, 0 };
        IUnknown *unk = nullptr;
        HRESULT hr = m_surface->BeginDraw(nullptr, __uuidof(ID3D11Texture2D),
                                          reinterpret_cast<void **>(&unk),
                                          &offset);
        if (SUCCEEDED(hr)) {
            ID3D11Texture2D *tex = nullptr;
            hr = unk->QueryInterface(&tex);
            unk->Release();
            if (SUCCEEDED(hr)) {
                m_ctx->UpdateSubresource(tex, 0, nullptr, image.bits(),
                                         image.bytesPerLine(), 0);
                tex->Release();
            }
            hr = m_surface->EndDraw();
            if (SUCCEEDED(hr)) {
                m_dcomp->Commit();
                return;
            }
        }
        // 设备丢失/瞬态失败：整链重建后重试一次（自纠重建后再重传）
        qWarning("[band] 帧上传失败 hr=0x%08lX（第 %d 次，整链重建重试）",
                 (unsigned long)hr, attempt + 1);
        releaseDComp();
        if (!initDComp())
            return;
    }
}

// ---------- 原生消息与交互派发 ----------

qintptr DesktopBandHost::nativeMessage(int message, quintptr wParam,
                                       qintptr lParam)
{
    switch (message) {
    case WM_PAINT:
        ValidateRect(hwndOf(m_hwnd), nullptr);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE; // 带内窗口永不激活
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL: // T-048-FIX（P2-2）：滚轮同走转发链
        forwardMouse(message, wParam, lParam);
        return 0;
    case WM_MOUSELEAVE:
        sendLeave();
        return 0;
    case WM_DESTROY:
        m_hwnd = nullptr;
        return 0;
    default:
        return qintptr(DefWindowProcW(hwndOf(m_hwnd), UINT(message),
                                      WPARAM(wParam), LPARAM(lParam)));
    }
}

// 转发的鼠标消息 → QMouseEvent/QContextMenuEvent 派发给隐藏的内容控件。
// 客户区物理坐标 → 逻辑局部坐标；命中子控件（childAt）逐层投递，
// 按压事件沿父链传播（Qt 同款语义），交互逻辑保持单一来源。
void DesktopBandHost::forwardMouse(int nativeMsg, quintptr wParam,
                                   qintptr lParam)
{
    if (!m_content)
        return;
    const RouteMsg rmsg = classifyNativeMsg(nativeMsg);
    // T-048-FIX（P2-2）：滚轮 lParam 为屏幕物理坐标（Windows 约定，区别
    // 于按钮消息的客户区坐标），先换算宿主客户区再走统一派发
    if (rmsg == RouteMsg::Wheel) {
        const QPointF screenPhys(qreal(short(LOWORD(lParam))),
                                 qreal(short(HIWORD(lParam))));
        dispatchWheel(screenPhys - m_nativeRect.topLeft(),
                      short(HIWORD(wParam)));
        emit interactionRouted(int(rmsg));
        return;
    }
    const QPointF clientPhys(qreal(short(LOWORD(lParam))),
                             qreal(short(HIWORD(lParam))));
    const QPointF local = clientPhys / m_dpr;
    // 客户区 → 宿主屏幕位置 → 逻辑全局（菜单定位等用）。
    // T-048-FIX（P2-2 坐标链统一时顺带修正）：宿主屏幕原点即
    // m_nativeRect.topLeft()（placeWindow 的 rel 换算可证：bandOffset +
    // rel == topLeft），原式再累加 m_bandOffset 属重复计数（band 客户区
    // 原点非 (0,0) 时菜单会整体偏移）
    const QPointF globalLogical =
        (m_nativeRect.topLeft() + clientPhys) / m_dpr;
    const Qt::MouseButtons buttons = buttonsFromWp(wParam);
    switch (rmsg) {
    case RouteMsg::LeftDown:
        dispatchMouseEvent(QEvent::MouseButtonPress, Qt::LeftButton, buttons,
                           local, globalLogical);
        break;
    case RouteMsg::LeftUp:
        dispatchMouseEvent(QEvent::MouseButtonRelease, Qt::LeftButton, buttons,
                           local, globalLogical);
        break;
    case RouteMsg::LeftDbl:
        dispatchMouseEvent(QEvent::MouseButtonDblClick, Qt::LeftButton, buttons,
                           local, globalLogical);
        break;
    case RouteMsg::Move:
        dispatchMouseEvent(QEvent::MouseMove, Qt::NoButton, buttons, local,
                           globalLogical);
        break;
    case RouteMsg::RightDown:
        dispatchMouseEvent(QEvent::MouseButtonPress, Qt::RightButton, buttons,
                           local, globalLogical);
        break;
    case RouteMsg::RightUp:
        dispatchMouseEvent(QEvent::MouseButtonRelease, Qt::RightButton, buttons,
                           local, globalLogical);
        // 右键抬启 = Qt Windows 平台的上下文菜单时机：派发给内容控件，
        // 由其既有逻辑处理（日历空白=透传桌面菜单、列表=条目菜单）
        dispatchContextMenu(local, globalLogical);
        break;
    default:
        return;
    }
    emit interactionRouted(int(rmsg));
}

void DesktopBandHost::dispatchMouseEvent(QEvent::Type type,
                                         Qt::MouseButton button,
                                         Qt::MouseButtons buttons,
                                         const QPointF &local,
                                         const QPointF &globalLogical)
{
    QWidget *content = m_content.data();
    if (!content)
        return;
    QWidget *w = content->childAt(local.toPoint());
    if (!w)
        w = content;
    for (;;) {
        const QPointF lp = (w == content) ? local : w->mapFrom(content, local);
        QMouseEvent ev(type, lp, globalLogical, button, buttons,
                       currentKeyModifiers());
        QApplication::sendEvent(w, &ev);
        // 仅按压沿父链向上传播（QApplication 同款）；移动/释放单点投递
        if (ev.isAccepted() || type != QEvent::MouseButtonPress)
            break;
        QWidget *parent = w->parentWidget();
        if (!parent || w == content)
            break;
        w = parent;
    }
}

void DesktopBandHost::dispatchContextMenu(const QPointF &local,
                                          const QPointF &globalLogical)
{
    QWidget *content = m_content.data();
    if (!content)
        return;
    QWidget *w = content->childAt(local.toPoint());
    if (!w)
        w = content;
    for (;;) {
        const QPoint lp =
            ((w == content) ? local : w->mapFrom(content, local)).toPoint();
        QContextMenuEvent ev(QContextMenuEvent::Mouse, lp,
                             globalLogical.toPoint());
        QApplication::sendEvent(w, &ev);
        if (ev.isAccepted() || w == content)
            break;
        QWidget *parent = w->parentWidget();
        if (!parent)
            break;
        w = parent;
    }
}

void DesktopBandHost::sendLeave()
{
    if (!m_content)
        return;
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(m_content.data(), &leave);
    emit interactionRouted(int(RouteMsg::Leave));
}

// T-048-FIX（P2-2）：滚轮 → QWheelEvent 派发给命中子控件（日历列表滚动
// 单一来源），坐标链与鼠标消息一致（客户区物理 → 逻辑局部/全局）
void DesktopBandHost::dispatchWheel(const QPointF &clientPhys, int delta)
{
    QWidget *content = m_content.data();
    if (!content)
        return;
    const QPointF local = clientPhys / m_dpr;
    const QPointF globalLogical = (m_nativeRect.topLeft() + clientPhys) / m_dpr;
    QWidget *w = content->childAt(local.toPoint());
    if (!w)
        w = content;
    QWheelEvent ev(local, globalLogical, QPoint(), QPoint(0, delta),
                   Qt::NoButton, currentKeyModifiers(), Qt::NoScrollPhase,
                   false);
    QApplication::sendEvent(w, &ev);
}

// ---------- 带内自纠（5s 低频，零漂移零开销） ----------

void DesktopBandHost::checkBandHealth()
{
    if (!m_active)
        return;
    // 宿主窗本体被销毁（Explorer 重建壁纸带时子窗随之销毁）→ 整链重建
    if (!m_hwnd || !IsWindow(hwndOf(m_hwnd))) {
        qWarning("[band] 宿主窗口丢失（壁纸带被重建？）——整链重建驻入");
        // T-048-FIX（P1-1）：重建前呈现态——重建后按原态恢复。编辑模式/
        // 打字挂起中带呈现在关（m_bandShown=false），重建不得把它复活
        //（真窗口与带内容双份同屏）；正常带呈现中则照常恢复
        const bool wasShown = m_bandShown;
        releaseDComp();
        m_active = false;
        const QRect logical = m_logicalRect;
        if (activate(logical, m_title, m_layer)) {
            if (wasShown)
                showBandInternal(); // 呈现进行中 → 恢复（内容门旁路）
            if (!m_lastImage.isNull())
                presentImage(m_lastImage);
            return;
        }
        // T-048-FIX（P1-3）：重建失败不残留半激活态——彻底释放后交调用
        // 方降级（deactivate 停 heal/注销路由/释放 DComp 与窗口）
        deactivate();
        emit bandLost();
        return;
    }
    // 归属检查：直接父窗必须仍是驻入的壁纸带 WorkerW。用 GA_PARENT 而非
    // GA_ROOT——壁纸带在本机（Progman 子级形态）及 WE 布局下 GA_ROOT 会
    // 是 Progman 而非带本身，直接父窗判定对两种形态都成立
    const HWND parent = GetAncestor(hwndOf(m_hwnd), GA_PARENT);
    if (parent != m_band) {
        const HWND band = locateBandBlocking();
        if (!band) {
            qWarning("[band] 带归属漂移且重定位失败——deactivate 后交调用方降级");
            // T-048-FIX（P1-3）：重绑失败不再留 m_active=true 的静默僵尸
            deactivate();
            emit bandLost();
            return;
        }
        // T-048-FIX（P1-3）：SetParent 失败同走 bandLost（原先静默 return
        // ——m_active=true 但永不再渲染、永不降级的僵尸宿主）；定位结果
        // 与旧带同值（句柄复用）也照常重绑，不跳过
        if (!SetParent(hwndOf(m_hwnd), band)) {
            qWarning("[band] SetParent 重绑失败 err=%lu——deactivate 后交调用方降级",
                     GetLastError());
            deactivate();
            emit bandLost();
            return;
        }
        m_band = band;
        refreshBandOffset();
        releaseDComp(); // 重绑 target（Parent 变更后重建，防隐形）
        if (!initDComp()) {
            qWarning("[band] 重绑 DComp 初始化失败——deactivate 后交调用方降级");
            deactivate();
            emit bandLost();
            return;
        }
        // T-048-FIX（P1-3）：空图（尚无内容帧）只跳过表面创建/上传——
        // 原先空图会让 ensureSurface(QSize(0,0)) 失败并落入静默僵尸分支；
        // 几何照常归位（防位置滞后）
        if (!m_lastImage.isNull())
            uploadImage(m_lastImage);
        placeWindow(m_logicalRect); // 归位
        return;
    }
    // T-048-FIX（P3-3）：WorkerW 平移不重建场景——heal tick 顺带刷新带
    // 客户区原点，漂移即重新归位（零漂移时仅一次 ClientToScreen，零写屏）
    const QPoint prevOffset = m_bandOffset;
    refreshBandOffset();
    if (m_bandOffset != prevOffset)
        placeWindow(m_logicalRect);
    // 带内 Z 序自纠：盒子层在日历层之上（与 DesktopStack 次序对齐）。
    // 零漂移时仅链表遍历比较，无 SetWindowPos 调用
    BandMouseRouter::instance()->assertBandZOrder(m_band);
}

} // namespace desktopband
} // namespace poptools
