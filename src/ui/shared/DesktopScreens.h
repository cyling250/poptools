#pragma once

#include <QGuiApplication>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QScreen>

#include <windows.h>

namespace poptools {
namespace deskscreens {

// ============================================================
// 多屏几何 helper（T-063 移植）：QScreen 几何是 Qt 逻辑坐标，虚拟桌面物理
// 坐标经 Win32 显示器枚举获取。双屏同 DPI 精确；混 DPI 机器按逻辑中心取
// 最近显示器。供日历/收纳盒面板做"矩形归属哪块屏"判定与跨屏拖动。
// ============================================================

inline QList<QRect> nativeScreenRects()
{
    QList<QRect> out;
    const auto screens = QGuiApplication::screens();
    out.reserve(screens.size());
    for (QScreen *s : screens) {
        const QPoint c = s->geometry().center();
        const HMONITOR mon = ::MonitorFromPoint(
            POINT{ static_cast<LONG>(c.x()), static_cast<LONG>(c.y()) },
            MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (mon && ::GetMonitorInfoW(mon, &mi)) {
            out.append(QRect(mi.rcMonitor.left, mi.rcMonitor.top,
                             mi.rcMonitor.right - mi.rcMonitor.left,
                             mi.rcMonitor.bottom - mi.rcMonitor.top));
        }
    }
    return out;
}

// 整个虚拟桌面（所有屏的物理并集；原点=主屏左上 (0,0)）
inline QRect virtualDesktopPhysical()
{
    QRect u;
    for (const QRect &r : nativeScreenRects())
        u = u.united(r);
    return u;
}

// 含矩形中心的屏优先；其次重叠最大者；最终回退列表首屏（主屏，拔屏自愈）
inline QRect resolveScreenForRect(const QRect &r)
{
    const auto all = nativeScreenRects();
    if (all.isEmpty())
        return {};
    const QPoint c = r.center();
    for (const QRect &native : all)
        if (native.contains(c))
            return native;
    const QRect *best = nullptr;
    qint64 bestArea = -1;
    for (const QRect &native : all) {
        const QRect isec = native.intersected(r);
        const qint64 area = qint64(isec.width()) * qint64(isec.height());
        if (area > bestArea) {
            bestArea = area;
            best = &native;
        }
    }
    return best ? *best : all.first();
}

} // namespace deskscreens
} // namespace poptools
