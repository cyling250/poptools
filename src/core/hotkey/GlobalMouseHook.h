#pragma once

#include <QObject>
#include <QPoint>

namespace poptools {

// ============================================================
// Windows 全局低级鼠标钩子（WH_MOUSE_LL）
//
// 监听全系统任意位置的鼠标按下事件。用途：不抢焦点的浮窗
// （如划词翻译卡片）收不到本应用之外的鼠标事件（Qt 事件过滤器
// 只能看到本应用内部的事件），无法据此实现"点击外部关闭"，
// 需要系统级钩子感知。
//
// 用法：
//   GlobalMouseHook hook;
//   hook.start();   // 卡片显示期间启用，隐藏即 stop，避免常驻开销
//   connect(&hook, &GlobalMouseHook::mousePressed, this, [](const QPoint &pos){ ... });
//   hook.stop();
//
// 基于 SetWindowsHookEx(WH_MOUSE_LL)（user32.lib）；钩子回调在安装线程
// （Qt 主线程）中执行，信号为直接连接，槽内勿做耗时操作（低级钩子有超时限制）。
// ============================================================
class GlobalMouseHook : public QObject
{
    Q_OBJECT
public:
    explicit GlobalMouseHook(QObject *parent = nullptr);
    ~GlobalMouseHook() override;

    bool isRunning() const { return m_hook != nullptr; }
    // 安装钩子（已安装时为空操作）
    void start();
    // 卸载钩子（未安装时为空操作）
    void stop();

signals:
    // 系统任意位置按下鼠标（左/中/右键）。
    // nativePos 为物理像素的屏幕坐标（多屏/DPI 缩放下与 Qt 逻辑坐标不一致）。
    void mousePressed(const QPoint &nativePos);

private:
    void *m_hook = nullptr; // HHOOK（Win32 句柄存 void*，避免头文件引入 windows.h）
};

} // namespace poptools
