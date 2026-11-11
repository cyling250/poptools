#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QVector>
#include <deque>

#include "DesktopBandLogic.h"

class QPoint;

namespace poptools {

class GlobalMouseHook;

namespace desktopband {

class DesktopBandHost;

// ============================================================
// BandMouseRouter——壁纸带鼠标转发器（T-048）。
//
// 消费共享 WH_MOUSE_LL 钩子（GlobalMouseHook 吞噬过滤器，不新建系统
// 钩子，红线 2 豁免纪律）：钩子回调内零耗时只做矩形命中判定并入队，
// 判定与转发在事件循环——命中日历/收纳盒屏幕矩形 → 吞掉并 PostMessage
// 按钮/移动事件到宿主 HWND（屏幕坐标→宿主客户区坐标换算）；未命中
// → 放行（点击图标/桌面菜单由系统自然处理）。
//
// Qt 弹出菜单/模态对话框打开期间一律放行：它们是原生顶层窗口，
// 吞掉会吃掉菜单点击。T-048-FIX（P1-2/P2-1）：他应用按压拖拽横穿带矩形
// 时 Move 放行（未捕获 + GetAsyncKeyState 判定）；菜单模态/系统 Shell
// 弹层（开始菜单等）前台时一律放行（GetGUIThreadInfo 级廉价判定）。
// ============================================================
class BandMouseRouter : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(BandMouseRouter)
public:
    static BandMouseRouter *instance(); // qApp 生命周期单例

    void addHost(DesktopBandHost *host);    // 激活宿主注册（幂等）
    void removeHost(DesktopBandHost *host); // 注销（最后一个注销即卸钩）
    void markRectsDirty();                  // 宿主几何/显隐变化 → 重算缓存
    void resetRouteState();                 // 清捕获/hover 状态（宿主隐藏时）
    void assertBandZOrder(void *band);      // 带内 Z 序自纠（宿主层序对齐）

private:
    explicit BandMouseRouter(QObject *parent);

    struct HostEntry
    {
        QPointer<DesktopBandHost> host;
    };

    void rebuildRectCache();
    // 钩子回调线程（主线程）调用：纯判定 + 入队，返回是否吞噬。
    // nativeData = MSLLHOOKSTRUCT::mouseData（滚轮 delta 高字，P2-2）
    bool swallowFilter(int nativeMsg, const QPoint &nativePos,
                       quintptr nativeData);
    void scheduleDrain();
    void drain(); // 事件循环：坐标换算 + PostMessage 转发

    QHash<int, HostEntry> m_hosts;
    int m_nextId = 0;
    QVector<QRectF> m_rectCache;
    QVector<int> m_zRanks; // T-048-FIX（P3-8）：带内 Z 序（大 = 靠顶）
    bool m_rectsDirty = true;

    RouteState m_state;
    struct Pending
    {
        RouteOutcome outcome;
        QPointF nativePos;
        int wheelDelta = 0; // T-048-FIX（P2-2）：滚轮 delta（转发时入 wParam 高字）
    };
    std::deque<Pending> m_queue;
    bool m_drainScheduled = false;

    GlobalMouseHook *m_hook = nullptr;
    quintptr m_filterId = 0;
    int m_dblClickMs = 500;
    // T-048-FIX（P3-7）：双击距离窗取系统度量（替换曼哈顿 ≤6 硬编码）
    int m_dblDistX = 4;
    int m_dblDistY = 4;
};

} // namespace desktopband
} // namespace poptools
