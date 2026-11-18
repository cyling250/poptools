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
//
// 优先级：裸键热键（modifiers == 0，如 F1/F3）额外挂一层本进程
// WH_KEYBOARD_LL 低级钩子——命中即吞键并排队触发，压过其他软件的
// 低级钩子/加速键（如把 F1 绑成"帮助"的软件），达到用户态最高优先级；
// 钩子被系统摘除或安装失败时自动退回纯 RegisterHotKey 语义。注册成功
// 判定以"任一机制可用"为准。机理与红线2豁免说明见 GlobalHotKey.cpp
// 文件头注释块。
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
    // 最近一次注册失败的原因（"与 PopTools 内其他快捷键冲突" / "已被其他程序占用" /
    // "该热键已在本类注册" / Win32 错误码），注册成功后清空
    QString lastError() const { return m_lastError; }

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
    QString m_lastError;
};

} // namespace poptools
