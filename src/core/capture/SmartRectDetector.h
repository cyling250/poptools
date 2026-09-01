#pragma once

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QThread>

namespace snipaste {

// ============================================================
// 智能框选矩形探测器：查询鼠标所在点的「元素级」矩形（窗口内
// 输入框 / 按钮 / 面板 / 列表项…），供截图预选高亮。
//
// 三层来源，取包含查询点的最小矩形：
//   1) 顶层窗口：EnumWindows 按 Z 序几何命中 + DWM 扩展边界
//      （剔除 Win10 不可见 resize 边框与 cloaked 不可见窗口）
//   2) 原生子控件：EnumChildWindows 找包含点的最小可见子窗口
//   3) UIA 控件树：从顶层窗口元素沿 ControlView 下行，只走包含
//      点的分支，取最深元素的 BoundingRectangle——覆盖浏览器 /
//      Electron / WPF / Qt 等无子 HWND 的现代界面元素
//
// 关键约束：截图时全屏覆盖层挡住一切，任何「命中测试」类 API
// （WindowFromPoint / IUIAutomation::ElementFromPoint）只会命中
// 覆盖层自身，因此全程只用「枚举 + 几何包含」判定；UIA 从目标
// 窗口句柄走树，而非从桌面按点查。UIA 元素最小粒度为控件 / 文本
// 行，结构上不会出现文字笔画闭合区域（字母环）这类误选。
//
// 坐标约定（与 ScreenCapturer 一致）：
//   物理全局 = 逻辑全局 × 所在屏 dpr；查询用物理坐标，结果由
//   提交时的 dpr 换回逻辑全局矩形。
//
// 线程模型：内部常驻工作线程（COM STA + IUIAutomation 会话）。
// submit() 线程安全，忙时只保留最新查询（latest-wins），完成后
// 自动补查；结果经 found() 信号（队列连接）回到调用方线程。
// 非 Windows 平台查询返回空矩形。
// ============================================================
class SmartRectDetector : public QObject
{
    Q_OBJECT
public:
    explicit SmartRectDetector(QObject *parent = nullptr);
    ~SmartRectDetector() override;

    // 提交一次查询（线程安全）。physPos 为物理全局坐标，dpr 为该点
    // 所在屏的缩放比（结果矩形换回逻辑坐标时使用），gen 为捕获代次
    // （过期结果由调用方丢弃），excludeHwnd 为需排除的窗口（截图覆盖层）。
    void submit(const QPoint &physPos, qreal dpr, quint64 gen, void *excludeHwnd);

signals:
    // 一轮检测完成（自工作线程发出）：logicalRect 为逻辑全局矩形，
    // 空矩形表示该点无可识别目标
    void found(quint64 gen, const QRect &logicalRect);

private:
    class Worker;
    Worker *m_worker = nullptr;
    QThread m_thread;
};

} // namespace snipaste
