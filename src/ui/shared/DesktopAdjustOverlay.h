#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QWidget>

#include <functional>

namespace poptools {

// ============================================================
// 调整位置和大小覆盖层（照搬 rlqd CalendarClone/AdjustOverlayForm.cs，
// T-063 拖动体验重制）：全屏半透明分层窗，内圈虚线框——边缘 14px 缩放、
// 内部拖动、✓ 确认，最小 520x420；拖动范围 = 整个虚拟桌面（矩形中心跨屏
// 即整体换屏），无取消语义（拖到哪里算哪里，Esc/Enter/✓ 均为就地确认）。
//
// 拖动跟手（T-063 根治三件套）：①日历面板预览直接画在本覆盖层（provider
// 返回宿主渲染的预览帧，四周 margin 余量容纳阴影/发光），跨进程擦除链路
// 退出拖动感知路径；②窗口固定覆盖所在屏（拖动中零窗口移动——半透明窗口
// 移动经过鼠标位置会合成新 mouse 事件喂回队列，事件洪水随拖动时长累积，
// 即"越拖越不跟手+闪烁"的根因），仅跨屏瞬间切屏一次；③拖动位移用全局
// 坐标差值（窗口切屏时局部坐标原点跳变会让面板卡死在边界）。
// 内部一律 Qt 逻辑全局坐标（物理=虚拟桌面坐标经原点感知换算构造），
// changed/confirmed 回调换回虚拟桌面物理坐标交还宿主。
// ============================================================
class DesktopAdjustOverlay : public QWidget
{
    Q_OBJECT
public:
    // calendarRect/screen 均为虚拟桌面物理像素；screenLogical 为拖动范围（全
    // 虚拟桌面）；screenRectsLogical 为各屏逻辑几何（窗口跨屏切换）；预览帧
    // 四周含 32px 余量。无取消语义：✓/Enter/Esc 均为就地确认
    DesktopAdjustOverlay(const QRect &calendarRectPhysical, const QRect &screenPhysical,
                         const QRect &screenLogical, const QList<QRect> &screenRectsLogical,
                         qreal dpr, std::function<QImage()> previewProvider,
                         std::function<void(const QRect &)> onChangedPhysical,
                         std::function<void(const QRect &)> onConfirmedPhysical,
                         QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    int hitZone(const QPoint &localPt) const;
    QRect nativeToLogical(const QRect &physical) const;
    QRect logicalToNative(const QRect &logical) const;
    void updateBoundsFromInner();

    std::function<QImage()> m_preview; // 宿主渲染的面板预览帧（含边距余量）
    int m_previewMargin = 32;          // 预览帧四周余量（阴影/发光溢出）
    QList<QRect> m_screenRects;        // 各屏逻辑几何（窗口跨屏切换用）
    QRect m_windowRect;                // 当前窗口覆盖的屏（拖动中窗口零移动防事件自激）
    std::function<void(const QRect &)> m_changed;
    std::function<void(const QRect &)> m_confirmed;
    qreal m_dpr = 1.0;
    QRect m_screenNative;   // 拖动范围（虚拟桌面物理）
    QPoint m_nativeOrigin;  // 虚拟桌面物理原点（=主屏左上）
    QPoint m_logicalOrigin; // 虚拟桌面 Qt 逻辑原点
    QRect m_screen;         // 拖动范围逻辑坐标（位移钳制用）
    QRect m_inner;          // 面板（逻辑全局）
    QRect m_startRect;
    QPoint m_startGlobal; // 按下时全局逻辑坐标：位移用全局差值（跨屏连续）
    int m_zone = 0;
    bool m_dragging = false;
    QRect m_checkRect;
};

} // namespace poptools
