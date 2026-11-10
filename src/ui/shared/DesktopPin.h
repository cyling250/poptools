#pragma once

#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace poptools {
namespace desktoppin {

// ============================================================
// 桌面贴底：窗口保持【顶层】，Z 序钉在 Progman（系统桌面窗口，
// Z 序最底）正上方——一切普通窗口天然都在其上，视觉上
// 「贴在桌面上、被所有窗口遮挡」，且鼠标直达、渲染必然正常。
//
// 为什么不用 SetParent 进 Progman/WorkerW（Wallpaper Engine /
// Rainmeter 的经典技法）：本机 Win11 24H2+ 实测（P-031），外来
// GDI 子窗口钉进 Progman 及其 WorkerW 子级后可交互（右键/点击
// 都有响应）但内容永远不被 DWM 合成——不透明化、摘 WS_EX_LAYERED、
// 清 alpha 格式均无效。顶层窗口 + 贴 Z 序是本机唯一稳定的
// 「贴桌面」实现。
//
// 使用：窗口显示后调用 glueToDesktop()，并以约 1s 周期重复调用
//（系统 Z 序被搅动时自动归位；已就位时仅一次 GetWindow 比较，
// 零开销）。
// ============================================================

// 把窗口 Z 序调整为紧贴桌面（Progman 正上方）。已就位时不做任何
// SetWindowPos。返回是否处于贴底状态。
inline bool glueToDesktop(QWidget *w)
{
#ifdef Q_OS_WIN
    if (!w->isVisible())
        return false;
    HWND hwnd = reinterpret_cast<HWND>(w->winId());
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman)
        return false;

    // 已紧贴桌面（Z 序中紧跟 Progman 之前）则不动
    if (GetWindow(hwnd, GW_HWNDNEXT) == progman)
        return true;

    // 放到「桌面正前方那扇窗口」的后面 = Progman 正上方
    HWND anchor = GetWindow(progman, GW_HWNDPREV);
    SetWindowPos(hwnd, anchor ? anchor : HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return GetWindow(hwnd, GW_HWNDNEXT) == progman;
#else
    Q_UNUSED(w);
    return false;
#endif
}

} // namespace desktoppin
} // namespace poptools
