#pragma once

#include <QPointF>
#include <QRectF>
#include <QVector>

namespace poptools {
namespace desktopband {

// ============================================================
// T-048 桌面壁纸带纯逻辑：band 定位决策 / 降级决策 / 钩子转发路由
// 状态机 / 上传合并节流。全部为无副作用纯函数或纯状态（无窗口系统
// 调用），供 DesktopBandHost / BandMouseRouter 使用，并可在
// tests/domain_tests.cpp 直接单测（注入句柄/坐标，不碰真实窗口）。
// ============================================================

// ---- band 定位决策：注入探测结果句柄，决策下一步动作 ----
struct BandProbeInput
{
    const void *progman = nullptr; // Progman 顶层窗口
    const void *defview = nullptr; // Progman 下的 SHELLDLL_DefView（图标层）
    // FindWindowEx(progman, defview, "WorkerW")——图标层之后的带兄弟窗
    const void *workerAfterDefview = nullptr;

    // 其余顶层 WorkerW 及其是否挂有 SHELLDLL_DefView 子窗
    //（图标层缺席时的带判定依据）
    struct TopWorker
    {
        const void *hwnd = nullptr;
        bool hasDefViewChild = false;
    };
    QVector<TopWorker> topWorkers;
};

enum class BandVerdict
{
    Ready,  // 壁纸带就绪（band 字段有效）
    Splice, // 需向 Progman 发 0x052C 触发分裂后重探（幂等）
    Fail    // 无法定位（无 Progman 等）
};

struct BandProbeResult
{
    BandVerdict verdict = BandVerdict::Fail;
    const void *band = nullptr;
};

BandProbeResult resolveBand(const BandProbeInput &input);

// ---- 降级决策：bandMode 逃生口开关与定位结果 → 呈现路径 ----
enum class PresentPath
{
    Band,      // 驻入壁纸带（DesktopBandHost 直绘 + 钩子转发交互）
    GlueStack  // 既有路径：真窗口 + DesktopStack Z 序贴底守护
};
PresentPath choosePresentPath(bool bandModeEnabled, bool bandLocated);

// ---- 钩子转发路由（WH_MOUSE_LL 命中判定与状态机，纯函数） ----
enum class RouteMsg
{
    None = 0,
    LeftDown, LeftUp, LeftDbl, Move,
    RightDown, RightUp,
    Wheel, // T-048-FIX（P2-2）：WM_MOUSEWHEEL 命中吞掉并路由到宿主
    Leave // 非吞噬 Move 划出宿主矩形（宿主向内容控件发 QEvent::Leave 清 hover）
};

// 路由状态机（跨鼠标事件保持；band id 为路由器注册序号）
struct RouteState
{
    int capturedLeftBand = -1;  // 左键拖拽捕获的宿主（按下设定、抬起清除）
    int capturedRightBand = -1;
    int hoverBand = -1;         // 最后一次 Move 命中的宿主
    int lastLeftDownBand = -1;  // 双击判定：上次左键按下（宿主/时刻/位置）
    qint64 lastLeftDownMs = 0;
    QPointF lastLeftDownPos;
};

struct RouteOutcome
{
    bool swallow = false;  // true = 钩子层吞掉（系统/图标层收不到）
    int band = -1;         // 吞噬时的转发目标宿主 id
    int leaveBand = -1;    // 非吞噬 Move 划出时需发 Leave 的宿主 id
    RouteMsg msg = RouteMsg::None;
    QPointF nativePos;     // 物理屏幕坐标（转发前换算宿主客户区）
};

// bandRects 按宿主 id 索引（空矩形 = 未激活宿主）。nativeMsg 为
// WH_MOUSE_LL 的 wParam 消息（NC 变体在内部归一为对应客户区消息）；
// 中键/X 键等一律放行（None）。dblClickMs 为 GetDoubleClickTime()；
// dblDistX/Y 为系统双击距离窗 SM_CXDOUBLECLK/SM_CYDOUBLECLK
//（T-048-FIX P3-7：替换曼哈顿 ≤6 硬编码，第二击须落在第一击
// dblDistX×dblDistY 矩形内，开区间）。buttonHeld（T-048-FIX P1-2）：
// 任意鼠标键按下中（钩子层以 GetAsyncKeyState 判定后注入）——未捕获的
// Move 一律放行且不更新 hover（外来按压拖拽横穿不得被悬停吞噬冻结）。
// zRanks（T-048-FIX P3-8）：各宿主在带内 Z 序（大 = 靠顶，与 bandRects
// 平行；未提供/无排名时退回注册序首命中）。
RouteOutcome routeMouse(RouteState &state, const QVector<QRectF> &bandRects,
                        int nativeMsg, const QPointF &nativePos,
                        qint64 nowMs, int dblClickMs, int dblDistX,
                        int dblDistY, bool buttonHeld,
                        const QVector<int> &zRanks = QVector<int>());

// 屏幕物理坐标 → 宿主客户区物理坐标（带客户区原点即其屏幕矩形左上）
QPointF toClientPhysical(const QPointF &nativePos, const QRectF &bandNativeRect);

// LL 钩子消息 → 路由消息（未知消息 → None；NC 变体归一为客户区消息；
// T-048-FIX P2-2：WM_MOUSEWHEEL → Wheel 参与路由，中键/X 键仍放行）
RouteMsg classifyNativeMsg(int nativeMsg);

// 路由消息 → 转发用的原生窗口消息（Leave 由 WM_MOUSELEAVE 承载）
int nativeMsgForRoute(RouteMsg msg);

// ---- 上传合并节流：单次事件循环至多一次 DComp Commit（性能红线） ----
class UploadCoalescer
{
public:
    // 提交一帧：true = 调用者需调度一次 flush；false = 已有调度，帧被合并
    bool submit()
    {
        m_pending = true;
        if (m_scheduled)
            return false;
        m_scheduled = true;
        return true;
    }
    // flush 入口：取走待传标志并清调度位；false = 无待传帧（空转）
    bool beginFlush()
    {
        const bool pending = m_pending;
        m_pending = false;
        m_scheduled = false;
        return pending;
    }

private:
    bool m_scheduled = false;
    bool m_pending = false;
};

} // namespace desktopband
} // namespace poptools
