#include "GlobalMouseHook.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace poptools {

#ifdef Q_OS_WIN
namespace {

// 钩子回调是 C 函数指针，无法携带 this，用静态指针转发到实例。
// 本类全局仅创建一个实例（划词翻译卡片是全局唯一的）。
GlobalMouseHook *g_mouseHookInstance = nullptr;

LRESULT CALLBACK mouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    // 仅关心按下：任意键按下都通知（含客户区/非客户区两种消息形态），
    // 是否属于"点击外部"由使用方结合落点窗口归属判断
    if (nCode >= 0 && g_mouseHookInstance) {
        const UINT msg = static_cast<UINT>(wParam);
        if (msg == WM_LBUTTONDOWN || msg == WM_NCLBUTTONDOWN
            || msg == WM_RBUTTONDOWN || msg == WM_NCRBUTTONDOWN
            || msg == WM_MBUTTONDOWN) {
            const auto *info = reinterpret_cast<const MSLLHOOKSTRUCT *>(lParam);
            // 排队转发（原先在钩子回调内同步 emit）：低级钩子回调有严格超时，
            // 槽若耗时超限会被系统静默摘钩且无提示；排队把处理挪回事件循环，
            // 回调本身只做一次入队
            QMetaObject::invokeMethod(g_mouseHookInstance, "mousePressed",
                                      Qt::QueuedConnection,
                                      Q_ARG(QPoint, QPoint(info->pt.x, info->pt.y)));
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

} // namespace
#endif

GlobalMouseHook::GlobalMouseHook(QObject *parent)
    : QObject(parent)
{
#ifdef Q_OS_WIN
    // 全局仅一个实例（划词翻译卡片全局唯一）：二次构造会覆盖静态指针，
    // 使第一个实例的钩子回调路由到错误对象
    Q_ASSERT_X(g_mouseHookInstance == nullptr, "GlobalMouseHook",
               "GlobalMouseHook 只允许一个实例（划词卡片为全局唯一浮窗）");
    g_mouseHookInstance = this;
#endif
}

GlobalMouseHook::~GlobalMouseHook()
{
    stop();
#ifdef Q_OS_WIN
    if (g_mouseHookInstance == this)
        g_mouseHookInstance = nullptr;
#endif
}

void GlobalMouseHook::start()
{
#ifdef Q_OS_WIN
    if (m_hook)
        return;
    m_hook = SetWindowsHookEx(WH_MOUSE_LL, mouseProc, GetModuleHandle(nullptr), 0);
#endif
}

void GlobalMouseHook::stop()
{
#ifdef Q_OS_WIN
    if (m_hook) {
        UnhookWindowsHookEx(reinterpret_cast<HHOOK>(m_hook));
        m_hook = nullptr;
    }
#endif
}

} // namespace poptools
