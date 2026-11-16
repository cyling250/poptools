#pragma once

#include <QRect>
#include <QWidget>

#include <functional>

namespace poptools {

// ============================================================
// 收纳盒调整覆盖层（T-060，与日历 DesktopAdjustOverlay 同族但语义收敛）：
// 尺寸纪律 = 格子整数倍，不允许自由缩放——
//   * 内部拖动 = 移动位置（x/y 跟随）
//   * 右缘 14px 拖动 = 改列数（吸附 2~8 列，高度/宽度由 BoxLayout::compute
//     自动重排，预览即最终态）
//   * ✓/× 确认取消；Esc 取消 / Enter 确认
// rectProvider 每帧回取面板当前矩形（列数变了面板重排，虚线框实时跟随）。
// 内部一律 Qt 逻辑坐标（物理 ÷ dpr 构造，回调换回物理）。
// ============================================================
class BoxAdjustOverlay : public QWidget
{
    Q_OBJECT
public:
    BoxAdjustOverlay(const QRect &panelPhysical, const QRect &screenPhysical, qreal dpr,
                     std::function<void(const QPoint &topLeftPhysical, int cols)> onChanged,
                     std::function<void(const QPoint &topLeftPhysical, int cols)> onConfirmed,
                     std::function<void()> onCanceled,
                     std::function<QRect()> rectProvider,
                     QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QRect currentLogical() const;       // 面板矩形（物理 → 逻辑）
    QRect checkRectNow() const;         // ✓ 实时锚定面板右上
    QRect cancelRectNow() const;        // × 实时锚定面板右上
    int hitZone(const QPoint &localPt) const; // 0 无 / 1 内部 / 2 右缘 / 3 按钮
    int colsForWidth(int widthPhysical) const;
    QRect toPhysical(const QRect &logical) const;

    std::function<void(const QPoint &, int)> m_changed;
    std::function<void(const QPoint &, int)> m_confirmed;
    std::function<void()> m_canceled;
    std::function<QRect()> m_rectProvider;
    qreal m_dpr = 1.0;
    QRect m_screen; // 逻辑坐标
    QPoint m_startTopLeft; // 物理
    int m_startCols = 4;
    QPoint m_startLocal;
    int m_zone = 0;
    bool m_finished = false;
    QRect m_checkRect;
    QRect m_cancelRect;
};

} // namespace poptools
