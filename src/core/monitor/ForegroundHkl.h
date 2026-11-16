#pragma once

#ifdef Q_OS_WIN
#include <windows.h>

namespace poptools {

// 前台窗口线程的键盘布局（HKL）：KeyboardTextComposer::translateVk（按布局
// 翻译按键）与 ImeCommitDetector::imeGate（CJK 布局门）共用的小块探测
//（T-062 S-4：原两处 GetForegroundWindow → GetWindowThreadProcessId →
// GetKeyboardLayout 三连逐语句重复）。inline 直通，无状态无副作用；
// 前台窗口不可知时 tid=0，GetKeyboardLayout 回退调用线程自己的布局
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
