#include "DesktopBandLogic.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace poptools {
namespace desktopband {

BandProbeResult resolveBand(const BandProbeInput &in)
{
    BandProbeResult r;
    if (!in.progman)
        return r; // Fail：连 Progman 都不在（Explorer 未就绪/锁屏会话）

    if (in.defview) {
        if (in.workerAfterDefview) {
            // 标准分裂形态：Progman ← [DefView(图标层), WorkerW(壁纸带)]
            r.verdict = BandVerdict::Ready;
            r.band = in.workerAfterDefview;
        } else {
            // 图标层在但壁纸带未分裂：0x052C 幂等触发
            r.verdict = BandVerdict::Splice;
        }
        return r;
    }

    // 图标层缺席（瞬态/主题差异）：顶层 WorkerW 中无 DefView 子窗者即带
    for (const BandProbeInput::TopWorker &w : in.topWorkers) {
        if (w.hwnd && !w.hasDefViewChild) {
            r.verdict = BandVerdict::Ready;
            r.band = w.hwnd;
            return r;
        }
    }
    r.verdict = BandVerdict::Splice;
    return r;
}

PresentPath choosePresentPath(bool bandModeEnabled, bool bandLocated)
{
    // T-048 逃生口：calendar/bandMode=false 或带定位失败 → 既有贴底路径
    //（功能不丢，只是层级形态回退）
    return (bandModeEnabled && bandLocated) ? PresentPath::Band
                                            : PresentPath::GlueStack;
}

RouteMsg classifyNativeMsg(int msg)
{
#ifdef Q_OS_WIN
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_NCLBUTTONDOWN:
        return RouteMsg::LeftDown;
    case WM_LBUTTONUP:
    case WM_NCLBUTTONUP:
        return RouteMsg::LeftUp;
    case WM_MOUSEMOVE:
    case WM_NCMOUSEMOVE:
        return RouteMsg::Move;
    case WM_RBUTTONDOWN:
    case WM_NCRBUTTONDOWN:
        return RouteMsg::RightDown;
    case WM_RBUTTONUP:
    case WM_NCRBUTTONUP:
        return RouteMsg::RightUp;
    case WM_MOUSEWHEEL:
        return RouteMsg::Wheel; // T-048-FIX（P2-2）：滚轮命中即吞并路由
    default:
        return RouteMsg::None; // 中键/X 键/横向滚轮等一律放行
    }
#else
    Q_UNUSED(msg)
    return RouteMsg::None;
#endif
}

int nativeMsgForRoute(RouteMsg msg)
{
#ifdef Q_OS_WIN
    switch (msg) {
    case RouteMsg::LeftDown:
        return WM_LBUTTONDOWN;
    case RouteMsg::LeftUp:
        return WM_LBUTTONUP;
    case RouteMsg::LeftDbl:
        return WM_LBUTTONDBLCLK;
    case RouteMsg::Move:
        return WM_MOUSEMOVE;
    case RouteMsg::RightDown:
        return WM_RBUTTONDOWN;
    case RouteMsg::RightUp:
        return WM_RBUTTONUP;
    case RouteMsg::Wheel:
        return WM_MOUSEWHEEL; // T-048-FIX（P2-2）
    case RouteMsg::Leave:
        return WM_MOUSELEAVE;
    case RouteMsg::None:
        break;
    }
#else
    Q_UNUSED(msg)
#endif
    return 0;
}

QPointF toClientPhysical(const QPointF &nativePos, const QRectF &bandNativeRect)
{
    return nativePos - bandNativeRect.topLeft();
}

RouteOutcome routeMouse(RouteState &s, const QVector<QRectF> &rects,
                        int nativeMsg, const QPointF &pos,
                        qint64 nowMs, int dblMs, int dblDistX, int dblDistY,
                        bool buttonHeld, const QVector<int> &zRanks)
{
    RouteOutcome out;
    const RouteMsg msg = classifyNativeMsg(nativeMsg);
    if (msg == RouteMsg::None)
        return out;

    // T-048-FIX（P3-8）：矩形重叠时按带内 Z 序取优先（zRanks 大 = 靠顶，
    // 与 DesktopStack 层序一致）；未提供排名时退回注册序首命中（既有行为）
    const auto hit = [&rects, &pos, &zRanks]() {
        int best = -1;
        for (int i = 0; i < rects.size(); ++i) {
            if (!rects.at(i).contains(pos))
                continue;
            if (best < 0)
                best = i;
            else if (zRanks.size() > qMax(i, best)
                     && zRanks.at(i) > zRanks.at(best))
                best = i;
        }
        return best;
    };

    switch (msg) {
    case RouteMsg::LeftDown: {
        const int band = hit();
        if (band < 0)
            return out; // 未命中：放行（点击图标/桌面菜单自然处理）
        s.capturedLeftBand = band;
        out.swallow = true;
        out.band = band;
        out.nativePos = pos;
        // 双击合成：LL 钩子只见原始按下，双击消息由转发层按
        // 时间窗 + 距离窗判定后合成（T-048-FIX P3-7：距离窗改用系统
        // 度量 SM_CXDOUBLECLK/SM_CYDOUBLECLK 的开区间矩形语义）
        const QPointF delta = pos - s.lastLeftDownPos;
        const bool dbl = band == s.lastLeftDownBand
                         && nowMs - s.lastLeftDownMs <= qint64(dblMs)
                         && qAbs(delta.x()) < qreal(dblDistX)
                         && qAbs(delta.y()) < qreal(dblDistY);
        out.msg = dbl ? RouteMsg::LeftDbl : RouteMsg::LeftDown;
        if (dbl) {
            s.lastLeftDownBand = -1; // 双击后重开新对
            s.lastLeftDownMs = 0;
        } else {
            s.lastLeftDownBand = band;
            s.lastLeftDownMs = nowMs;
            s.lastLeftDownPos = pos;
        }
        return out;
    }
    case RouteMsg::LeftUp: {
        // 拖拽捕获期间全程吞噬（可在宿主矩形外抬起），保证按下/抬起配对
        if (s.capturedLeftBand < 0)
            return out;
        out.swallow = true;
        out.band = s.capturedLeftBand;
        out.msg = RouteMsg::LeftUp;
        out.nativePos = pos;
        s.capturedLeftBand = -1;
        return out;
    }
    case RouteMsg::Move: {
        if (s.capturedLeftBand >= 0) {
            // 拖拽捕获期间跟随（可出界，负坐标交 Qt 处理）
            out.swallow = true;
            out.band = s.capturedLeftBand;
            out.msg = RouteMsg::Move;
            out.nativePos = pos;
            return out;
        }
        // T-048-FIX（P1-2）：按压拖拽中（外来拖拽横穿带矩形）→ Move 放行
        // 且不更新 hover——悬停吞噬会冻结任意应用的拖窗口/划选/滑块/OLE
        // 拖拽。钩子层以 GetAsyncKeyState 判定后经 buttonHeld 注入（纯函数
        // 可测）；已捕获的拖拽（本带发起）不受影响，保持配对路由
        if (buttonHeld)
            return out;
        const int band = hit();
        if (band >= 0) {
            s.hoverBand = band;
            out.swallow = true;
            out.band = band;
            out.msg = RouteMsg::Move;
            out.nativePos = pos;
            return out;
        }
        if (s.hoverBand >= 0) {
            // 划出：放行（图标层 hover 自然处理）+ 通知宿主清 hover
            out.leaveBand = s.hoverBand;
            s.hoverBand = -1;
            return out;
        }
        return out;
    }
    case RouteMsg::RightDown: {
        const int band = hit();
        if (band < 0)
            return out; // 未命中：放行（桌面右键菜单透传）
        s.capturedRightBand = band;
        out.swallow = true;
        out.band = band;
        out.msg = RouteMsg::RightDown;
        out.nativePos = pos;
        return out;
    }
    case RouteMsg::RightUp: {
        if (s.capturedRightBand < 0)
            return out;
        out.swallow = true;
        out.band = s.capturedRightBand;
        out.msg = RouteMsg::RightUp;
        out.nativePos = pos;
        s.capturedRightBand = -1;
        return out;
    }
    case RouteMsg::Wheel: {
        // T-048-FIX（P2-2）：滚轮命中宿主矩形 → 吞掉并路由（带模式下
        // 日历列表才能滚动）；未命中放行。不改变捕获/hover 状态
        const int band = hit();
        if (band < 0)
            return out;
        out.swallow = true;
        out.band = band;
        out.msg = RouteMsg::Wheel;
        out.nativePos = pos;
        return out;
    }
    default:
        return out;
    }
}

} // namespace desktopband
} // namespace poptools
