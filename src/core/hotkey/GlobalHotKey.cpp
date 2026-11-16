#include "GlobalHotKey.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000 // Win7+（旧 SDK 头缺失时兜底，语义见 Win32 文档）
#endif
#endif

namespace poptools {

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
    const bool ok = RegisterHotKey(nullptr, id, modifiers | MOD_NOREPEAT, vk) != 0;
    if (ok)
        m_lastError.clear();
    else if (GetLastError() == ERROR_HOTKEY_ALREADY_REGISTERED)
        m_lastError = QStringLiteral("已被其他程序占用");
    else
        m_lastError = QStringLiteral("注册失败（Win32 错误码 %1）").arg(GetLastError());
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
#endif
    it->registered = false;
    m_keys.erase(it);
}

bool GlobalHotKey::isRegistered(int id) const
{
    return m_keys.contains(id) && m_keys[id].registered;
}

// WM_HOTKEY 分发：仅命中本类已注册 id 时发 activated 并吞掉消息（返回 true），其余放行
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
