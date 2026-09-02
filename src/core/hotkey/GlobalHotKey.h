#pragma once

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QHash>

namespace poptools {

// ============================================================
// Windows 全局热键封装
//
// 用法：
//   GlobalHotKey hotkey;
//   hotkey.registerKey(0x70 /* VK_F1 */, CaptureHotKeyId);
//   connect(&hotkey, &GlobalHotKey::activated, this, [](int id){ ... });
//
// 基于 RegisterHotKey / UnregisterHotKey（user32.lib）。
// ============================================================
class GlobalHotKey : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit GlobalHotKey(QObject *parent = nullptr);
    ~GlobalHotKey() override;

    // 注册全局热键（vk 为虚拟键码，modifiers 为 MOD_ALT/MOD_CONTROL 等组合，默认 0 无修饰键）
    // 成功返回 true；重复注册返回 false
    bool registerKey(quint32 vk, int id, quint32 modifiers = 0);
    // 注销指定 id 的热键
    void unregisterKey(int id);
    // 指定 id 是否已注册
    bool isRegistered(int id) const;

signals:
    // 热键被按下（id 与 registerKey 传入的一致）
    void activated(int id);

protected:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    struct Entry {
        quint32 vk = 0;
        quint32 modifiers = 0;
        bool registered = false;
    };
    QHash<int, Entry> m_keys;
};

} // namespace poptools
