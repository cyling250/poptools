#pragma once

#include <QObject>
#include <QPoint>

namespace poptools {

// ============================================================
// Windows 全局低级鼠标钩子（WH_MOUSE_LL）——共享钩子，多消费者。
//
// 监听全系统任意位置的鼠标按下事件。用途：不抢焦点的浮窗
//（划词翻译卡片、Dock 文件夹弹层）收不到本应用之外的鼠标事件
//（Qt 事件过滤器只能看到本应用内部的事件），无法据此实现
// "点击外部关闭"，需要系统级钩子感知。
//
// 用法：
//   GlobalMouseHook hook;
//   hook.start();   // 浮窗显示期间启用，隐藏即 stop，避免常驻开销
//   connect(&hook, &GlobalMouseHook::mousePressed, this, [](const QPoint &pos){ ... });
//   hook.stop();
//
// 多个浮窗可能并存（划词卡片 + Dock 弹层）：内部用引用计数共用
// 一个系统钩子，事件广播给全部已 start 的实例；任一实例 stop 后
// 计数归零才卸载钩子。
//
// 基于 SetWindowsHookEx(WH_MOUSE_LL)（user32.lib）；钩子回调在安装
// 线程（Qt 主线程）中执行，信号经队列转发到事件循环处理（回调内
// 不做任何耗时工作——低级钩子有超时限制，超限会被系统静默摘钩）。
// ============================================================
class GlobalMouseHook : public QObject
{
    Q_OBJECT
public:
    explicit GlobalMouseHook(QObject *parent = nullptr);
    ~GlobalMouseHook() override;

    bool isRunning() const { return m_active; }
    // 加入共享钩子（首次调用安装系统钩子）
    void start();
    // 离开共享钩子（最后一个实例离开时卸载系统钩子）
    void stop();

signals:
    // 系统任意位置按下鼠标（左/中/右键）。
    // nativePos 为物理像素的屏幕坐标（多屏/DPI 缩放下与 Qt 逻辑坐标不一致）。
    void mousePressed(const QPoint &nativePos);

private:
    bool m_active = false; // 本实例是否在消费共享钩子（系统钩子句柄为全局静态）
};

} // namespace poptools
