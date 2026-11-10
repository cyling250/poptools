#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <functional>

namespace poptools {

// 一段"前台窗口 + 输入活动"记录（timeline.jsonl 的一行）
struct TimelineSegment
{
    QString app;      // 进程名（不含路径，小写）
    QString title;    // 窗口标题
    qint64 startMs = 0; // epoch ms
    qint64 endMs = 0;
    int activeSec = 0;    // 活跃秒数（有键/鼠输入的秒）
    int keystrokes = 0;   // 键入次数
    int clicks = 0;       // 点击次数
    bool idleEnded = false; // 以空闲超时结束（end=最后一次输入时刻）
};
Q_DECLARE_METATYPE(poptools::TimelineSegment)

// ============================================================
// 键鼠输入状态机：把键盘/鼠标事件聚合成"带活跃度标注的窗口时间线"。
//
// 设计取舍（用户已对齐）：
//   * 鼠标事件不逐条落盘——单条点击无语义，其价值是活跃度信号；
//   * 段在【切窗 / 空闲超时 / 同窗满 60s 切片】时闭合，
//     长会话切片既控单行体量也抗崩溃丢数据；
//   * 窗口只被"路过"（无任何输入）不开段——记录的是工作，
//     不是桌面截图式的全量窗口流水；
//   * 同 hwnd 标题变化（浏览器换标签页）只更新标题不切段。
//
// 纯状态机：时钟经 setClock 注入（测试用虚拟时钟），无内部定时器，
// 由模块的 1s 心跳驱动 tick()。
// ============================================================
class InputActivityMonitor : public QObject
{
    Q_OBJECT
public:
    explicit InputActivityMonitor(QObject *parent = nullptr);

    void setIdleThresholdSec(int sec) { m_idleThresholdMs = sec * 1000; }
    // 测试注入时钟；默认取系统 epoch ms
    void setClock(const std::function<qint64()> &now) { m_now = now; }

public slots:
    // 前台窗口变化（ForegroundTracker 转发）
    void setWindowContext(quintptr hwnd, const QString &app, const QString &title);

    void noteKeydown(); // 键盘钩子 down 事件
    void noteClick();   // 鼠标按下（GlobalMouseHook 消费）

    // 1s 心跳：活跃计数 + 空闲判定 + 长段切片
    void tick();

    // 立即闭合当前段（停用监控/退出时）
    void flush();

signals:
    void segmentClosed(const poptools::TimelineSegment &seg);

private:
    qint64 now() const;
    void openSegment();
    void closeSegment(bool idleEnded);

    int m_idleThresholdMs = 120 * 1000;
    std::function<qint64()> m_now;

    bool m_hasCur = false;
    TimelineSegment m_cur;
    qint64 m_lastInputMs = 0;

    // 记忆的窗口上下文：段闭合后（空闲）同窗新输入要能原位续开
    quintptr m_lastHwnd = 0;
    QString m_lastApp, m_lastTitle;
};

} // namespace poptools
