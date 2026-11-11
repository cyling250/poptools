#include "GlobalMouseHook.h"

#include <QPair>
#include <QSet>
#include <QVector>

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

// T-048：吞噬过滤器注册表（带窗宿主命中判定）。仅主线程访问（钩子
// 回调在安装线程执行，add/remove 也只从主线程调用），无需加锁。
QVector<QPair<quintptr, GlobalMouseHook::SwallowPredicate>> g_swallowFilters;
quintptr g_nextFilterId = 0;

LRESULT CALLBACK mouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0) {
        const UINT msg = static_cast<UINT>(wParam);
        const auto *info = reinterpret_cast<const MSLLHOOKSTRUCT *>(lParam);
        // T-048：吞噬判定（谓词零阻塞，纯矩形判定 + 入队）。必须与
        // 广播同处回调内——吞噬要在系统投递前同步生效，事后拦截不可能
        bool swallowed = false;
        for (auto it = g_swallowFilters.cbegin(); it != g_swallowFilters.cend();
             ++it) {
            if (it->second(int(msg), QPoint(info->pt.x, info->pt.y),
                           quintptr(info->mouseData))) { // T-048-FIX（P2-2）：带 mouseData（滚轮 delta）
                swallowed = true;
                break;
            }
        }
        // 仅关心按下：任意键按下都广播（含客户区/非客户区两种消息形态），
        // 是否属于"点击外部"由各消费方结合落点窗口归属自行判断。
        // 吞噬不影响广播：监控/划词卡片仍视其为一次点击活动
        if (!g_hookConsumers.isEmpty()) {
            if (msg == WM_LBUTTONDOWN || msg == WM_NCLBUTTONDOWN
                || msg == WM_RBUTTONDOWN || msg == WM_NCRBUTTONDOWN
                || msg == WM_MBUTTONDOWN) {
                // 排队转发（回调内不做耗时工作）：低级钩子回调有严格超时，
                // 槽若耗时超限会被系统静默摘钩且无提示；排队把处理挪回
                // 事件循环，回调本身只做入队
                for (GlobalMouseHook *consumer : g_hookConsumers) {
                    QMetaObject::invokeMethod(consumer, "mousePressed", Qt::QueuedConnection,
                                              Q_ARG(QPoint, QPoint(info->pt.x, info->pt.y)));
                }
            }
        }
        if (swallowed)
            return 1; // 吞掉：系统层（图标层/桌面菜单）收不到该事件
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

} // namespace
#endif

quintptr GlobalMouseHook::addSwallowFilter(const SwallowPredicate &pred)
{
    // T-048-FIX（P3-1）：自增移入 Q_OS_WIN 分支——g_nextFilterId 的声明
    // 只在 Windows 匿名命名空间内存在，非 Windows 编译不再引用未声明名
#ifdef Q_OS_WIN
    const quintptr id = ++g_nextFilterId;
    g_swallowFilters.append({ id, pred });
    return id;
#else
    Q_UNUSED(pred)
    return 0;
#endif
}

void GlobalMouseHook::removeSwallowFilter(quintptr id)
{
#ifdef Q_OS_WIN
    for (int i = 0; i < g_swallowFilters.size(); ++i) {
        if (g_swallowFilters.at(i).first == id) {
            g_swallowFilters.removeAt(i);
            return;
        }
    }
#else
    Q_UNUSED(id)
#endif
}

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
