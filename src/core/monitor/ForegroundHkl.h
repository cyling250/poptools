#pragma once

#ifdef Q_OS_WIN
#include <windows.h>

namespace poptools {

// 前台窗口线程的键盘布局（HKL）：ImeCommitDetector::imeGate（CJK 布局门）
// 使用的小块探测（T-062 S-4：原两处 GetForegroundWindow →
// GetWindowThreadProcessId → GetKeyboardLayout 三连逐语句重复；原共用方
// KeyboardTextComposer 已随 T-066 字母流移除）。inline 直通，无状态无
// 副作用；前台窗口不可知时 tid=0，GetKeyboardLayout 回退调用线程自己的布局
inline HKL foregroundKeyboardLayout()
{
    const HWND fg = GetForegroundWindow();
    DWORD tid = 0;
    if (fg)
        tid = GetWindowThreadProcessId(fg, nullptr);
    return GetKeyboardLayout(tid);
}

} // namespace poptools
#endif // Q_OS_WIN
