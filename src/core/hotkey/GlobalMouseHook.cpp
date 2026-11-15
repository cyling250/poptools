#include "GlobalMouseHook.h"

#include <QSet>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace poptools {

#ifdef Q_OS_WIN
namespace {

// 共享钩子状态：多个浮窗（划词卡片 / Dock 弹层）并存时共用一个
// 系统钩子，事件广播给全部已 start 的实例；计数归零才卸载。
QSet<GlobalMouseHook *> g_hookConsumers;
HHOOK g_hook = nullptr;

LRESULT CALLBACK mouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0) {
        const UINT msg = static_cast<UINT>(wParam);
        const auto *info = reinterpret_cast<const MSLLHOOKSTRUCT *>(lParam);
        // 仅关心按下：任意键按下都广播（含客户区/非客户区两种消息形态），
        // 是否属于"点击外部"由各消费方结合落点窗口归属自行判断。
        if (!g_hookConsumers.isEmpty()) {
            if (msg == WM_LBUTTONDOWN || msg == WM_NCLBUTTONDOWN
                || msg == WM_RBUTTONDOWN || msg == WM_NCRBUTTONDOWN
                || msg == WM_MBUTTONDOWN) {
                const QPoint pt(info->pt.x, info->pt.y);
                // 排队转发（回调内不做耗时工作）：低级钩子回调有严格超时，
                // 槽若耗时超限会被系统静默摘钩且无提示；排队把处理挪回
                // 事件循环，回调本身只做入队
                for (GlobalMouseHook *consumer : g_hookConsumers) {
                    QMetaObject::invokeMethod(consumer, "mousePressed", Qt::QueuedConnection,
                                              Q_ARG(QPoint, pt));
                }
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

} // namespace
#endif

GlobalMouseHook::GlobalMouseHook(QObject *parent)
    : QObject(parent)
{
}

GlobalMouseHook::~GlobalMouseHook()
{
    stop();
}

void GlobalMouseHook::start()
{
    if (m_active)
        return;
#ifdef Q_OS_WIN
    g_hookConsumers.insert(this);
    if (!g_hook)
    {
        g_hook = SetWindowsHookEx(WH_MOUSE_LL, mouseProc, GetModuleHandle(nullptr), 0);
        if (!g_hook)
        {
            // 安装失败必须可见（R3 #20）：此前 m_active=true 但钩子未装，
            // "点击外部关闭"静默失效。不占消费名额、不置运行态，
            // isRunning() 如实反映钩子状态
            qWarning("GlobalMouseHook: SetWindowsHookEx(WH_MOUSE_LL) failed, error %lu",
                     GetLastError());
            g_hookConsumers.remove(this);
            return;
        }
    }
#endif
    m_active = true;
}

void GlobalMouseHook::stop()
{
    if (!m_active)
        return;
    m_active = false;
#ifdef Q_OS_WIN
    g_hookConsumers.remove(this);
    if (g_hookConsumers.isEmpty() && g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }
#endif
}

} // namespace poptools
