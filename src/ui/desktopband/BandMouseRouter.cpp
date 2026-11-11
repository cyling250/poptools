// T-048 壁纸带鼠标转发器实现：钩子回调内联命中判定（零阻塞）+ 事件
// 循环批量转发（PostMessage 到宿主 HWND）。链路设计与红线纪律见头注释。
#include "BandMouseRouter.h"

#include "DesktopBandHost.h"
#include "core/hotkey/GlobalMouseHook.h"

#include <QApplication>
#include <QDateTime>
#include <QPoint>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace poptools {
namespace desktopband {

#ifdef Q_OS_WIN
namespace {

// T-048-FIX（P2-1/P1-2）：钩子回调内廉价的线程/前台状态判定（均为本进程
// 内查询：GetGUIThreadInfo/GetForegroundWindow/GetClassName 不向其他线程
// 或进程发送任何消息，零阻塞，可留在回调谓词内）。

// 前台线程处于菜单模态（GUI_INMENUMODE：原生右键菜单/菜单栏下拉/托盘
// 菜单，含他应用）或系统移动/尺寸循环（GUI_INMOVESIZE，含键盘移动）
bool foregroundThreadInMenuOrMoveLoop()
{
    GUITHREADINFO gti = { sizeof(GUITHREADINFO) };
    return GetGUIThreadInfo(0, &gti)
           && (gti.flags & (GUI_INMENUMODE | GUI_INMOVESIZE)) != 0;
}

// 前台窗口是系统菜单 / Shell 弹层类（原生 #32768 菜单、Win10/11 开始菜单
// 与操作中心等 CoreWindow / XAML 宿主）——这些弹层与带矩形重叠时点击
// 必须放行。覆盖面为白名单内弹层；第三方自绘弹层不覆盖（部分覆盖取舍：
// 精确的 WindowFromPoint 归属查询会跨线程发 WM_NCHITTEST，挂起应用可拖
// 死系统输入，禁入钩子回调——见 swallowFilter 内注释）
bool foreignShellPopupForeground()
{
    const HWND fg = GetForegroundWindow();
    if (!fg)
        return false;
    wchar_t cls[48] = {};
    if (!GetClassNameW(fg, cls, 48))
        return false;
    static const wchar_t *kPopupClasses[] = {
        L"#32768",                              // 原生菜单（他应用右键/托盘菜单）
        L"Windows.UI.Core.CoreWindow",          // 开始菜单/操作中心宿主（Win10/11）
        L"XamlExplorerHostIslandWindow",        // Win11 Shell XAML 弹层宿主
        L"TopLevelWindowForOverflowXamlIsland", // Win11 任务栏溢出弹层
    };
    for (const wchar_t *k : kPopupClasses)
        if (wcscmp(cls, k) == 0)
            return true;
    return false;
}

} // namespace
#endif

BandMouseRouter *BandMouseRouter::instance()
{
    static QPointer<BandMouseRouter> s_instance;
    if (!s_instance) {
        QCoreApplication *app = QCoreApplication::instance();
        Q_ASSERT(app && "路由器单例须在 QApplication 之后创建");
        s_instance = new BandMouseRouter(app);
    }
    return s_instance.data();
}

BandMouseRouter::BandMouseRouter(QObject *parent)
    : QObject(parent)
{
#ifdef Q_OS_WIN
    m_dblClickMs = GetDoubleClickTime();
    // T-048-FIX（P3-7）：双击距离窗随系统设置（控制面板"双击速度"）
    m_dblDistX = qMax(1, GetSystemMetrics(SM_CXDOUBLECLK));
    m_dblDistY = qMax(1, GetSystemMetrics(SM_CYDOUBLECLK));
#endif
}

// ---------- 宿主注册 ----------

void BandMouseRouter::addHost(DesktopBandHost *host)
{
    if (!host)
        return;
    for (auto it = m_hosts.cbegin(); it != m_hosts.cend(); ++it) {
        if (it->host == host) {
            markRectsDirty(); // 已注册：刷新矩形即可
            return;
        }
    }
    const int id = m_nextId++;
    host->setRouteId(id);
    m_hosts.insert(id, HostEntry{ host });
    markRectsDirty();
    // 首个宿主：接管共享钩子消费（引用计数）并注册吞噬过滤器
    if (!m_hook) {
        m_hook = new GlobalMouseHook(this);
        m_hook->start();
    }
    if (!m_filterId) {
        m_filterId = GlobalMouseHook::addSwallowFilter(
            [this](int nativeMsg, const QPoint &nativePos, quintptr nativeData) {
                return swallowFilter(nativeMsg, nativePos, nativeData);
            });
    }
}

void BandMouseRouter::removeHost(DesktopBandHost *host)
{
    for (auto it = m_hosts.begin(); it != m_hosts.end(); ++it) {
        if (it->host == host) {
            m_hosts.erase(it);
            break;
        }
    }
    markRectsDirty();
    resetRouteState();
    if (m_hosts.isEmpty()) {
        // 最后一个宿主注销：摘过滤器并退出共享钩子（引用计数归零卸钩）
        if (m_filterId) {
            GlobalMouseHook::removeSwallowFilter(m_filterId);
            m_filterId = 0;
        }
        if (m_hook) {
            m_hook->stop();
            m_hook->deleteLater();
            m_hook = nullptr;
        }
    }
}

void BandMouseRouter::markRectsDirty()
{
    m_rectsDirty = true;
}

void BandMouseRouter::resetRouteState()
{
    m_state = RouteState();
}

// ---------- 命中判定与入队（钩子回调内联路径，零阻塞纪律） ----------

void BandMouseRouter::rebuildRectCache()
{
    int maxId = -1;
    HWND bandHwnd = nullptr;
    for (auto it = m_hosts.cbegin(); it != m_hosts.cend(); ++it) {
        if (it->host && it->host->active()) {
            maxId = qMax(maxId, it.key());
            if (!bandHwnd)
                bandHwnd = static_cast<HWND>(it->host->bandWindow());
        }
    }
    m_rectCache = QVector<QRectF>(maxId + 1); // 空矩形 = 未激活/隐藏宿主
    m_zRanks = QVector<int>(maxId + 1, -1);   // T-048-FIX（P3-8）
    for (auto it = m_hosts.cbegin(); it != m_hosts.cend(); ++it) {
        if (it->host && it->host->active() && it->host->bandVisible())
            m_rectCache[it.key()] = it->host->nativeRect();
    }
    // T-048-FIX（P3-8）：遍历带子链（GW_CHILD 自顶向下）取各宿主 Z 序，
    // 重叠矩形命中时按 Z 序取优先。GetWindow 为句柄遍历不发消息，与
    // assertBandZOrder 同级安全，可留在钩子回调重算路径
    if (bandHwnd && IsWindow(bandHwnd)) {
        int rank = 1 << 20;
        for (HWND child = GetWindow(bandHwnd, GW_CHILD); child && rank > 0;
             child = GetWindow(child, GW_HWNDNEXT), --rank) {
            for (auto it = m_hosts.cbegin(); it != m_hosts.cend(); ++it)
                if (it->host && it->host->nativeWindow() == child)
                    m_zRanks[it.key()] = rank;
        }
    }
    m_rectsDirty = false;
}

bool BandMouseRouter::swallowFilter(int nativeMsg, const QPoint &nativePos,
                                    quintptr nativeData)
{
    // 弹出菜单/模态对话框打开期间一律放行：它们是原生顶层窗口，
    // 点击菜单项由系统自然送达；吞掉会吃掉菜单点击
    if (QApplication::activePopupWidget() || QApplication::activeModalWidget())
        return false;
    const RouteMsg msg = classifyNativeMsg(nativeMsg);
    if (msg == RouteMsg::None)
        return false; // 中键/X 键/横向滚轮等不参与路由
#ifdef Q_OS_WIN
    // T-048-FIX（P2-1）：外来弹层放行——菜单模态（他应用右键/托盘菜单）
    // 或系统 Shell 弹层（开始菜单/操作中心）前台时一律放行。取舍：回调内
    // 只做 GetGUIThreadInfo/GetForegroundWindow/GetClassNameW 级零阻塞
    // 判定；精确的 WindowFromPoint 归属查询会跨线程发 WM_NCHITTEST
    //（挂起应用可拖死系统输入），禁入钩子回调，drain 阶段补判只能事后
    // 丢弃转发、救不回已被吞的事件，故不采用——覆盖面 = 原生菜单 + 系统
    // Shell 弹层白名单，第三方自绘弹层不覆盖（部分覆盖，首击无效再击即达）
    if (foregroundThreadInMenuOrMoveLoop() || foreignShellPopupForeground())
        return false;
    // T-048-FIX（P1-2）：未捕获 + 任一鼠标键按下 → Move 放行——悬停吞噬
    // 会打断任意应用的按压拖拽（拖窗口/划选/滑块/OLE 拖拽/拖图标横穿带
    // 矩形即冻结）。GetAsyncKeyState 零耗时、无跨进程，可留在回调谓词内；
    // 已捕获（本带发起）的拖拽保持原吞噬 + 配对路由。GUI_INMOVESIZE（含
    // 键盘移动循环）已在上面的放行前置中覆盖
    if (msg == RouteMsg::Move && m_state.capturedLeftBand < 0) {
        bool buttonHeld = false;
        for (const int vk : { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON }) {
            if (GetAsyncKeyState(vk) < 0) {
                buttonHeld = true;
                break;
            }
        }
        if (buttonHeld)
            return false;
    }
#endif
    if (m_rectsDirty)
        rebuildRectCache();
    if (m_rectCache.isEmpty())
        return false;
    const QPointF pos(nativePos);
    const RouteOutcome out =
        routeMouse(m_state, m_rectCache, nativeMsg, pos,
                   QDateTime::currentMSecsSinceEpoch(), m_dblClickMs,
                   m_dblDistX, m_dblDistY,
                   /*buttonHeld*/ false, m_zRanks);
    if (out.swallow || out.leaveBand >= 0) {
        // 连续 Move 合并（拖拽/hover 只需最新位置；滚轮不合并——各带
        // 独立 delta）
        if (out.msg == RouteMsg::Move && !m_queue.empty()
            && m_queue.back().outcome.msg == RouteMsg::Move)
            m_queue.back() = Pending{ out, pos, 0 };
        else
            m_queue.push_back(Pending{
                out, pos,
                out.msg == RouteMsg::Wheel
                    ? int(short(HIWORD(nativeData))) // T-048-FIX（P2-2）
                    : 0 });
        scheduleDrain();
    }
    return out.swallow;
}

void BandMouseRouter::scheduleDrain()
{
    if (m_drainScheduled)
        return;
    m_drainScheduled = true;
    QMetaObject::invokeMethod(this, &BandMouseRouter::drain,
                              Qt::QueuedConnection);
}

// ---------- 事件循环转发（判定结果 → 宿主 HWND） ----------

void BandMouseRouter::drain()
{
    m_drainScheduled = false;
    while (!m_queue.empty()) {
        const Pending p = m_queue.front();
        m_queue.pop_front();
        const RouteOutcome &out = p.outcome;

        // 非吞噬的划出 → 通知宿主清 hover（Leave）
        if (!out.swallow && out.leaveBand >= 0) {
            if (DesktopBandHost *h = m_hosts.value(out.leaveBand).host.data()) {
                if (h->bandVisible() && h->nativeWindow())
                    PostMessageW(HWND(h->nativeWindow()), WM_MOUSELEAVE, 0, 0);
            }
            continue;
        }
        if (!out.swallow || out.band < 0)
            continue;
        const auto it = m_hosts.constFind(out.band);
        if (it == m_hosts.constEnd())
            continue;
        DesktopBandHost *h = it->host.data();
        if (!h || !h->bandVisible() || !h->nativeWindow())
            continue;

        // T-048-FIX（P2-2）：滚轮转发——WM_MOUSEWHEEL 的 wParam 高字为
        // delta、lParam 为屏幕物理坐标（Windows 约定，区别于按钮消息的
        // 客户区坐标），由宿主侧换算客户区
        if (out.msg == RouteMsg::Wheel) {
            WPARAM wwp = 0;
            if (GetAsyncKeyState(VK_LBUTTON) < 0)
                wwp |= MK_LBUTTON;
            if (GetAsyncKeyState(VK_RBUTTON) < 0)
                wwp |= MK_RBUTTON;
            wwp |= WPARAM(static_cast<quint16>(static_cast<short>(p.wheelDelta)))
                   << 16;
            PostMessageW(HWND(h->nativeWindow()), WM_MOUSEWHEEL, wwp,
                         MAKELPARAM(int(p.nativePos.x()),
                                    int(p.nativePos.y())));
            continue;
        }

        // 屏幕物理坐标 → 宿主客户区物理坐标（纯函数换算）
        const QPointF client = toClientPhysical(p.nativePos, h->nativeRect());
        WPARAM wp = 0;
        switch (out.msg) {
        case RouteMsg::LeftDown:
        case RouteMsg::LeftDbl:
            wp = MK_LBUTTON;
            break;
        case RouteMsg::RightDown:
            wp = MK_RBUTTON;
            break;
        case RouteMsg::Move:
            // 移动的按键状态取实时键盘状态（拖拽中 MK 位保持）
            if (GetAsyncKeyState(VK_LBUTTON) < 0)
                wp |= MK_LBUTTON;
            if (GetAsyncKeyState(VK_RBUTTON) < 0)
                wp |= MK_RBUTTON;
            break;
        default:
            break;
        }
        PostMessageW(HWND(h->nativeWindow()),
                     UINT(nativeMsgForRoute(out.msg)), wp,
                     MAKELPARAM(int(client.x()), int(client.y())));
    }
}

// ---------- 带内 Z 序自纠 ----------

// 带内层序与 DesktopStack 对齐：盒子层在日历层之上。零漂移时仅链表
// 比较无 SetWindowPos 调用。
void BandMouseRouter::assertBandZOrder(void *band)
{
    const HWND bandHwnd = HWND(band);
    if (!bandHwnd || !IsWindow(bandHwnd))
        return;
    QVector<DesktopBandHost *> ours;
    for (const HostEntry &e : m_hosts) {
        if (e.host && e.host->active() && e.host->bandVisible()
            && e.host->bandWindow() == band)
            ours.append(e.host.data());
    }
    if (ours.isEmpty())
        return;
    // 期望（自顶向下）= 层序降序：盒子在上、日历在下。
    // T-048-FIX（P3-5）：stable_sort——同层双宿主（如多只收纳盒）键相等，
    // std::sort 每次可能给出不同次序，导致每 5s 自纠互相翻 churn
    std::stable_sort(ours.begin(), ours.end(),
                     [](DesktopBandHost *a, DesktopBandHost *b) {
                         return int(a->layer()) > int(b->layer());
                     });
    // 当前 GW_CHILD 链（自顶向下）中属于我们的窗口
    QVector<DesktopBandHost *> current;
    for (HWND child = GetWindow(bandHwnd, GW_CHILD); child;
         child = GetWindow(child, GW_HWNDNEXT)) {
        for (DesktopBandHost *h : ours)
            if (h->nativeWindow() == child)
                current.append(h);
    }
    bool ordered = current.size() == ours.size();
    for (int i = 0; ordered && i < current.size(); ++i)
        ordered = current.at(i) == ours.at(i);
    if (!ordered) {
        for (DesktopBandHost *h : ours) // 自底向上逐个置顶 → 层序落定
            SetWindowPos(HWND(h->nativeWindow()), HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

} // namespace desktopband
} // namespace poptools
