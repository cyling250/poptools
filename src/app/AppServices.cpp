#include "AppServices.h"

#include <QAction>
#include <QMenu>

#include "core/hotkey/GlobalHotKey.h"
#include "core/hotkey/HotKeyNames.h"

namespace poptools {

AppServices::AppServices(QObject *parent)
    : QObject(parent)
{
}

QAction *AppServices::addTrayAction(const QString &text, std::function<void()> triggered)
{
    if (!trayMenu)
        return nullptr;
    QAction *action = trayMenu->addAction(text);
    // context 对象用 action 自身：动作销毁时连接自动断开
    connect(action, &QAction::triggered, action,
            [triggered = std::move(triggered)](bool) {
                if (triggered)
                    triggered();
            });
    return action;
}

bool AppServices::registerHotKey(quint32 vk, quint32 modifiers,
                                 std::function<void()> triggered)
{
    if (hotkey && !m_hotkeyWired) {
        connect(hotkey, &GlobalHotKey::activated, this, &AppServices::onHotKeyActivated);
        m_hotkeyWired = true;
    }
    const int id = m_nextHotKeyId++;
    bool ok = false;
    if (hotkey) {
        ok = hotkey->registerKey(vk, id, modifiers);
        if (!ok)
            // 失败名带具体原因（"已被其他程序占用"/Win32 错误码），
            // 由组合根气泡透出——原先一律说"被占用"，参数错误时误导排查
            m_failedHotKeyNames.append(hotKeyDisplayName(vk, modifiers)
                                       + QStringLiteral("：") + hotkey->lastError());
    }
    m_hotkeyCallbacks.insert(id, std::move(triggered));
    return ok;
}

QStringList AppServices::takeFailedHotKeyNames()
{
    return std::move(m_failedHotKeyNames);
}

void AppServices::notifySettingsChanged()
{
    emit settingsChanged();
}

void AppServices::onHotKeyActivated(int id)
{
    if (const auto cb = m_hotkeyCallbacks.value(id))
        cb();
}

} // namespace poptools
