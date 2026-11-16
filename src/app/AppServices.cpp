#include "AppServices.h"

#include <QAction>
#include <QMenu>

#include "core/hotkey/GlobalHotKey.h"
#include "core/hotkey/HotKeyNames.h"

namespace poptools {

// 仅作 QObject；服务指针由组合根逐项注入（见 MainWindow 构造）
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
    } else {
        // T-062 修复: hotkey 未注入时原先静默失败且不留痕，组合根的失败气泡
        // 无从发——补记失败名。当前装配下不可达（组合根先注入后 initAll），
        // 纯防御口径补全，行为仅在不可达路径变化
        m_failedHotKeyNames.append(hotKeyDisplayName(vk, modifiers)
                                   + QStringLiteral("：内部热键注册器未注入"));
    }
    // 失败注册同样占位 id：保证 id 分配与回调映射一一对应；失败的 id 永不
    // 触发，无需清理
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

// 热键总线分发：按 GlobalHotKey 的 id 路由到注册时存下的回调；
// 无对应回调（如注册失败占位的 id）则静默忽略
void AppServices::onHotKeyActivated(int id)
{
    if (const auto cb = m_hotkeyCallbacks.value(id))
        cb();
}

} // namespace poptools
