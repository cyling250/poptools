#include "GlobalHotKey.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>
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
    if (m_keys.contains(id) && m_keys[id].registered)
        return false; // 已注册
#ifdef Q_OS_WIN
    const bool ok = RegisterHotKey(nullptr, id, modifiers, vk) != 0;
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
            return true;
        }
    }
#else
    Q_UNUSED(message)
#endif
    return false;
}

} // namespace poptools
