#pragma once

#include <QColor>
#include <QWidget>

#include <functional>

namespace poptools {

// ============================================================
// 调整位置和大小覆盖层（照搬 rlqd CalendarClone/AdjustOverlayForm.cs）：
// 全屏半透明分层窗，内圈虚线框——边缘 14px 缩放、内部拖动、✓/× 确认取消，
// 最小 520x420，钳制在主屏内；Esc 取消 / Enter 确认。
// 内部一律 Qt 逻辑坐标（物理坐标 ÷ dpr 构造，回调换回物理坐标交还宿主），
// changed 在拖动过程中持续回调（宿主实时重渲染共享位图）。
// ============================================================
class DesktopAdjustOverlay : public QWidget
{
    Q_OBJECT
public:
    // calendarRect/screen 均为物理像素；dpr 为主屏缩放比
    DesktopAdjustOverlay(const QRect &calendarRectPhysical, const QRect &screenPhysical,
                          qreal dpr, std::function<void(const QRect &)> onChangedPhysical,
                          std::function<void(const QRect &)> onConfirmedPhysical,
                          std::function<void()> onCanceled, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    int hitZone(const QPoint &localPt) const;
    QRect toPhysical(const QRect &logical) const;
    void updateBoundsFromInner();

    std::function<void(const QRect &)> m_changed;
    std::function<void(const QRect &)> m_confirmed;
    std::function<void()> m_canceled;
    qreal m_dpr = 1.0;
    QRect m_screen; // 逻辑坐标
    QRect m_inner;  // 逻辑坐标
    QRect m_startRect;
    QPoint m_startLocal;
    int m_zone = 0;
    bool m_dragging = false;
    QRect m_checkRect;
    QRect m_cancelRect;
};

} // namespace poptools
