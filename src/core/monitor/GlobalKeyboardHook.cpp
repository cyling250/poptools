#include "GlobalKeyboardHook.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace poptools {

#ifdef Q_OS_WIN
namespace {

// 单消费者（活动监控模块是唯一用户）；句柄与消费者指针为文件级静态
GlobalKeyboardHook *g_consumer = nullptr;
HHOOK g_hook = nullptr;

LRESULT CALLBACK keyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    // 回调内只做入队转发（低级钩子回调有严格超时，耗时会被系统静默摘钩）；
    // 修饰键 down/up 也要转发（接收方维护 Shift/Ctrl/Alt 状态机才能
    // 正确翻译 Shift+a → 'A'）
    if (nCode >= 0 && g_consumer) {
        const UINT msg = static_cast<UINT>(wParam);
        if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN
            || msg == WM_KEYUP || msg == WM_SYSKEYUP) {
            const auto *info = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
            const bool down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
            // 捕获时刻的焦点控件（GetGUIThreadInfo 只读线程状态，µs 级，
            // 不向目标进程发消息）：随事件入队后，打码判定在接收方按
            // 捕获时刻的焦点做样式探测，消除"处理时刻已切窗"的时点差
            GUITHREADINFO gi = {};
            gi.cbSize = sizeof(gi);
            const HWND focusHwnd = GetGUIThreadInfo(0, &gi) ? gi.hwndFocus : nullptr;
            QMetaObject::invokeMethod(g_consumer, "keyEvent", Qt::QueuedConnection,
                                      Q_ARG(int, static_cast<int>(info->vkCode)),
                                      Q_ARG(int, static_cast<int>(info->scanCode)),
                                      Q_ARG(bool, (info->flags & LLKHF_EXTENDED) != 0),
                                      Q_ARG(bool, down),
                                      Q_ARG(quintptr, reinterpret_cast<quintptr>(focusHwnd)));
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

} // namespace
#endif

GlobalKeyboardHook::GlobalKeyboardHook(QObject *parent)
    : QObject(parent)
{
}

GlobalKeyboardHook::~GlobalKeyboardHook()
{
    stop();
}

void GlobalKeyboardHook::start()
{
    if (m_active)
        return;
    m_active = true;
#ifdef Q_OS_WIN
    Q_ASSERT(!g_consumer && "GlobalKeyboardHook 目前只支持单消费者");
    g_consumer = this;
    g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, keyboardProc, GetModuleHandle(nullptr), 0);
    if (!g_hook) {
        // 装钩失败（安全软件拦截/权限策略）：回退为未激活，调用方按 isRunning 判断
        g_consumer = nullptr;
        m_active = false;
        qWarning("GlobalKeyboardHook: SetWindowsHookEx(WH_KEYBOARD_LL) 失败 err=%lu",
                 GetLastError());
    }
#endif
}

void GlobalKeyboardHook::stop()
{
    if (!m_active)
        return;
    m_active = false;
#ifdef Q_OS_WIN
    if (g_consumer == this)
        g_consumer = nullptr;
    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }
#endif
}

} // namespace poptools
