#pragma once

#include <QObject>

namespace poptools {

// ============================================================
// Windows 全局低级键盘钩子（WH_KEYBOARD_LL）——活动监控专用。
//
// 与 GlobalMouseHook 同族但独立成类：鼠标共享钩子只广播"按下"，
// 语义是"点击外部关闭"；键盘监控需要 down/up 全量（修饰键状态机
// 必须知道 Shift 何时松开），语义不同，不合并。
//
// 性能纪律（handover 红线2豁免条款：监控开启期间允许钩子常驻，
// 前提是回调零耗时）：钩子回调只做入队转发，打码判定/文本翻译/
// 磁盘写全部在接收方（主线程事件循环或 worker）完成——低级钩子
// 有超时限制，超限会被系统静默摘钩且无提示。
//
// 用法（单消费者）：
//   GlobalKeyboardHook hook;
//   hook.start();   // 监控开启期间常驻
//   connect(&hook, &GlobalKeyboardHook::keyEvent, this, [](int vk, int scan, bool ext, bool down){ ... });
//   hook.stop();
// ============================================================
class GlobalKeyboardHook : public QObject
{
    Q_OBJECT
public:
    explicit GlobalKeyboardHook(QObject *parent = nullptr);
    ~GlobalKeyboardHook() override;

    bool isRunning() const { return m_active; }
    void start();
    void stop();

signals:
    // 全系统键盘事件（down/up 均有，含被输入法吞掉前的原始键）。
    // vk 为 Windows 虚拟键码；scan 为扫描码；extended 是否扩展键。
    // 经队列转发，槽在主线程事件循环执行。
    void keyEvent(int vk, int scan, bool extended, bool down);

private:
    bool m_active = false;
};

} // namespace poptools
