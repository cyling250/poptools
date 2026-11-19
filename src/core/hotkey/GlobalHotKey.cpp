#include "GlobalHotKey.h"

#include <QCoreApplication>
#include <QVector>

#include "core/monitor/MainThreadProbe.h"

#ifdef Q_OS_WIN
#include <windows.h>
#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000 // Win7+（旧 SDK 头缺失时兜底，语义见 Win32 文档）
#endif
#endif

namespace poptools {

#ifdef Q_OS_WIN
namespace {

// ============================================================
// 裸键热键的优先级钩子（WH_KEYBOARD_LL）
//
// 机理：win32k 对一次物理击键的处理顺序是
//   ① 低级键盘钩子链（后装者先被调用；钩子吃键则整键蒸发）
//   ② RegisterHotKey 分发（命中 → WM_HOTKEY，键不进前台窗口）
//   ③ 常规派发（前台窗口 WM_KEYDOWN）
// RegisterHotKey 本就赢得过 ③ 的普通加速键（多数软件的"F1=帮助"即此类），
// 但会被 ① 里其他软件的低级钩子提前吞掉。裸键热键（无修饰键，如 F1/F3）
// 恰是与各软件内置触发键冲突的高发组合，故对裸键补装本进程的低级钩子：
// 命中即吞键并排队触发，把优先级顶到用户态天花板（仅剩"装得更晚且吞键"
// 的别家钩子能压过我们——那在用户态无解，重启本程序晚于它启动即可反压）。
// RegisterHotKey 保留作兜底：钩子被系统静默摘除（回调超时）或安装失败时
// 退回原行为；钩子吞掉的键到不了热键分发，两条路径对同一次按键互斥，
// 不会双触发。
//
// 红线2豁免（同活动监控 T-045 条款）：热键是常开功能，钩子随首个裸键
// 注册安装、最后一个注销卸载；回调零耗时——只做查表匹配与 Qt 队列入队，
// 触发动作全部回到主线程事件循环执行；非命中键原样 CallNextHookEx 放行。
// ============================================================

// 单消费者（组合根 MainWindow 持有唯一实例）；句柄/所有者/优先级键表为
// 文件级静态，对照 GlobalKeyboardHook 的 g_consumer 先例。钩子回调与
// registerKey/unregisterKey 同在主线程（钩子回调经该线程消息泵投递），
// 表无并发访问，无需加锁
GlobalHotKey *g_owner = nullptr;
HHOOK g_hook = nullptr;

struct PriorityRec {
    int id;            // GlobalHotKey 键 id（activated 信号按此回传）
    UINT vk;
    quint32 modifiers; // 当前恒为 0（只挂裸键）；保留字段，日后挂组合在此比对
    bool held;         // 本钩子已吞下对应 down——识别自动重复 + 配对吞掉 up
    DWORD lastDown;    // 最近一次吞下的 down 时刻（钩子自带 time），识别陈旧 held
};
QVector<PriorityRec> g_pri;

PriorityRec *findRec(UINT vk)
{
    for (auto &rec : g_pri) {
        if (rec.vk == vk)
            return &rec;
    }
    return nullptr;
}

// 修饰键实时态与注册要求严格相等：要求的必须在场，没要求的必须不在场
//（Shift+F1 不得命中裸 F1）。GetAsyncKeyState 读异步键表，µs 级，
// 不向任何进程发消息
bool modifiersMatch(quint32 required)
{
    static const struct { quint32 mod; int vk; } kMods[] = {
        {MOD_SHIFT, VK_SHIFT}, {MOD_CONTROL, VK_CONTROL}, {MOD_ALT, VK_MENU},
    };
    for (const auto &m : kMods) {
        const bool pressed = (GetAsyncKeyState(m.vk) & 0x8000) != 0;
        if ((required & m.mod) ? !pressed : pressed)
            return false;
    }
    const bool win = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0
                  || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
    return (required & MOD_WIN) ? win : !win;
}

LRESULT CALLBACK priorityKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    // T-068 取证打点：机理同 GlobalMouseHook（见 MainThreadProbe 头注释）
    const KBDLLHOOKSTRUCT *probeInfo =
        (nCode >= 0) ? reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam) : nullptr;
    const MainThreadProbe::HookCall probeCall(
        MainThreadProbe::SrcPriorityKey, probeInfo ? probeInfo->time : 0,
        probeInfo && (probeInfo->flags & LLKHF_INJECTED) != 0);
    if (nCode >= 0 && g_owner && !g_pri.isEmpty()) {
        const UINT msg = static_cast<UINT>(wParam);
        const bool down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        const bool up = (msg == WM_KEYUP || msg == WM_SYSKEYUP);
        if (down || up) {
            const auto *info = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
            if (PriorityRec *rec = findRec(info->vkCode)) {
                if (modifiersMatch(rec->modifiers)) {
                    if (down) {
                        // 重复识别：held 且距上次吞键 <2s 判为按住不放的自动重复
                        //（正常重复间隔毫秒级；2s 宽限兼顾"钩子漏掉 up 后的陈旧
                        // held"——此时视为新按触发，避免热键从此哑死）
                        const bool repeat = rec->held && (info->time - rec->lastDown) < 2000;
                        rec->lastDown = info->time;
                        if (!repeat) {
                            rec->held = true;
                            // 零耗时纪律：只入队，触发在主线程事件循环；
                            // 字符串签名须与 activated 形参表一致（失配=事件静默丢失）
                            QMetaObject::invokeMethod(g_owner, "activated",
                                                      Qt::QueuedConnection,
                                                      Q_ARG(int, rec->id));
                        }
                        // 吞键：重复键同样吞、只是不再触发（对齐 MOD_NOREPEAT 语义）
                        return 1;
                    }
                    if (rec->held) {
                        // 只配对吞掉自己吞过 down 的 up；别处漏出的裸 up 放行
                        rec->held = false;
                        return 1;
                    }
                }
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// 惰性装卸：首个裸键注册时装，g_pri 清空后卸。均在主线程调用——该线程
// 有 Qt 消息泵，钩子回调才能被系统投递
bool ensurePriorityHook()
{
    if (g_hook)
        return true;
    g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, priorityKeyboardProc,
                              GetModuleHandle(nullptr), 0);
    if (!g_hook) {
        // 装钩失败（安全软件拦截/权限策略）：退回纯 RegisterHotKey 语义，
        // 调用方按返回值决定注册结果的口径
        qWarning("GlobalHotKey: SetWindowsHookEx(WH_KEYBOARD_LL) 失败 err=%lu，"
                 "裸键热键退回 RegisterHotKey 优先级", GetLastError());
        return false;
    }
    return true;
}

void dropPriorityHookIfIdle()
{
    if (g_hook && g_pri.isEmpty()) {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }
}

} // namespace
#endif // Q_OS_WIN

GlobalHotKey::GlobalHotKey(QObject *parent)
    : QObject(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotKey::~GlobalHotKey()
{
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
    const auto ids = m_keys.keys();
    for (int id : ids)
        unregisterKey(id);
#ifdef Q_OS_WIN
    if (g_owner == this)
        g_owner = nullptr;
#endif
}

bool GlobalHotKey::registerKey(quint32 vk, int id, quint32 modifiers)
{
    if (m_keys.contains(id) && m_keys[id].registered) {
        // T-062 修复: 早退前刷新 lastError——契约是"最近一次注册失败的原因"，
        // 沿用上一次无关失败文案会误导调用方气泡（当前装配不可达，防御口径）
        m_lastError = QStringLiteral("该热键已在本类注册");
        return false; // 已注册
    }
#ifdef Q_OS_WIN
    // R5-D2：先查本进程内 vk+mod 重复——Windows 对同组合全局去重（含本
    // 进程），ini 把两个模块配成相同组合时第二次注册必败且 GetLastError
    // 报 ERROR_HOTKEY_ALREADY_REGISTERED，原文案"已被其他程序占用"把
    // 用户排查方向引向外部程序
    for (auto it = m_keys.cbegin(); it != m_keys.cend(); ++it) {
        if (it.key() == id || !it->registered)
            continue;
        if (it->vk == vk && it->modifiers == modifiers) {
            m_lastError = QStringLiteral("与 PopTools 内其他快捷键冲突");
            m_keys[id] = {vk, modifiers, false};
            return false;
        }
    }
    // T-062 修复: 补 MOD_NOREPEAT——按住不放时原先每秒重发多次 WM_HOTKEY，
    // 最重后果是按住 F3 约 1 秒连续创建十几个贴图窗（PinWindow 每次触发必新建）。
    // 只加在注册入参：Entry 仍存原始 modifiers，本进程冲突检测的相等比较与
    // hotKeyDisplayName 文案（按 0x1/0x2/0x4/0x8 取位）均不受影响
    const bool hotkeyOk = RegisterHotKey(nullptr, id, modifiers | MOD_NOREPEAT, vk) != 0;
    QString registerError;
    if (hotkeyOk)
        registerError.clear();
    else if (GetLastError() == ERROR_HOTKEY_ALREADY_REGISTERED)
        registerError = QStringLiteral("已被其他程序占用");
    else
        registerError = QStringLiteral("注册失败（Win32 错误码 %1）").arg(GetLastError());

    // 裸键（无修饰键）挂优先级钩子（机理见文件头注释块）。单消费者软防御：
    // 已有其他实例持有钩子时拒绝再挂（该实例退回纯 RegisterHotKey 语义，
    // 对照 GlobalKeyboardHook R6-C4 的写法）
    bool priorityOk = false;
    if (modifiers == 0) {
        if (g_owner && g_owner != this) {
            qWarning("GlobalHotKey: 已有其他实例 %p 持有优先级钩子，裸键退回"
                     " RegisterHotKey 优先级", static_cast<void *>(g_owner));
        } else {
            g_owner = this;
            if (ensurePriorityHook()) {
                g_pri.append({id, vk, modifiers, false, 0});
                priorityOk = true;
            }
        }
    }

    // 注册结果以"任一机制可用"为准：RegisterHotKey 被别处占用但钩子可用时，
    // 裸键照样生效且优先级更高（钩子在热键分发之前吞键），托盘文案不应再报
    // "已被占用"；两机制全败才走失败口径（registerError）
    const bool ok = hotkeyOk || priorityOk;
    if (ok)
        m_lastError.clear();
    else
        m_lastError = registerError;
#else
    const bool ok = false; // 其它平台暂未实现
    m_lastError = QStringLiteral("当前平台未实现热键");
#endif
    m_keys[id] = {vk, modifiers, ok};
    return ok;
}

void GlobalHotKey::unregisterKey(int id)
{
    auto it = m_keys.find(id);
    if (it == m_keys.end())
        return;
#ifdef Q_OS_WIN
    if (it->registered)
        UnregisterHotKey(nullptr, id);
    for (int i = 0; i < g_pri.size(); ++i) {
        if (g_pri[i].id == id) {
            g_pri.removeAt(i);
            break;
        }
    }
    dropPriorityHookIfIdle(); // 最后一个裸键注销即卸钩（惰性装卸的卸侧）
#endif
    it->registered = false;
    m_keys.erase(it);
}

bool GlobalHotKey::isRegistered(int id) const
{
    return m_keys.contains(id) && m_keys[id].registered;
}

// WM_HOTKEY 分发：仅命中本类已注册 id 时发 activated 并吞掉消息（返回 true），其余放行。
// 优先级钩子路径的 activated 走 QMetaObject::invokeMethod 队列（见 priorityKeyboardProc），
// 与本路径对同一次按键互斥（钩子吞掉的键到不了热键分发）
bool GlobalHotKey::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType)
    Q_UNUSED(result)
#ifdef Q_OS_WIN
    auto *msg = static_cast<MSG *>(message);
    if (msg->message == WM_HOTKEY) {
        const int id = static_cast<int>(msg->wParam);
        if (m_keys.contains(id) && m_keys[id].registered) {
            emit activated(id);
            // 吞掉消息：本类已消费该 WM_HOTKEY，后续原生过滤器与应用窗口不再收到
            return true;
        }
    }
#else
    Q_UNUSED(message)
#endif
    return false;
}

} // namespace poptools
