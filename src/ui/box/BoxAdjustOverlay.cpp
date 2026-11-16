#include "BoxAdjustOverlay.h"

#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include "core/box/BoxLayout.h"

#include <algorithm>

namespace poptools {

namespace {
constexpr int kEdgeGrab = 14; // 右缘列数热区（逻辑 px）
}

BoxAdjustOverlay::BoxAdjustOverlay(const QRect &panelPhysical, const QRect &screenPhysical,
                                   qreal dpr,
                                   std::function<void(const QPoint &, int)> onChanged,
                                   std::function<void(const QPoint &, int)> onConfirmed,
                                   std::function<void()> onCanceled,
                                   std::function<QRect()> rectProvider,
                                   QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_changed(std::move(onChanged))
    , m_confirmed(std::move(onConfirmed))
    , m_canceled(std::move(onCanceled))
    , m_rectProvider(std::move(rectProvider))
    , m_dpr(dpr > 0 ? dpr : 1.0)
{
    // WA_TranslucentBackground 必须保留：分层窗口仅在 alpha=0 像素处鼠标
    // 穿透——paintEvent 的满屏暗幕（alpha=110）铺满全域后即无穿透，且保持
    // 半透明视觉（不透明窗 + 半透明暗幕只会叠出全黑，已踩坑一次）。
    // 拖不动的历史根因是暗幕没画满屏（只画了虚线框线条），非 translucent 本身
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    // 关闭即销毁（P-058 第三课）：close 只 hide 不销毁的话，面板的
    // m_adjusting 永远卡 true，盒头按钮全部吞事件——表现为"卡死"
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    // screenPhysical 为物理像素 → 覆盖层几何用逻辑像素
    m_screen = QRect(QPoint(0, 0),
                     QPoint(qRound(screenPhysical.width() / m_dpr),
                            qRound(screenPhysical.height() / m_dpr)));
    setGeometry(m_screen);
    // 进入时快照不再存成员：Esc 取消用面板自带的 original（BoxPanel 侧），
    // ✓/Enter 确认一律走 rectProvider 的当前实时几何
    Q_UNUSED(panelPhysical)
}

QRect BoxAdjustOverlay::currentLogical() const
{
    const QRect panel = m_rectProvider ? m_rectProvider() : QRect();
    if (panel.isNull())
        return QRect();
    return QRect(qRound(panel.left() / m_dpr), qRound(panel.top() / m_dpr),
                 qRound(panel.width() / m_dpr), qRound(panel.height() / m_dpr));
}

// ✓/× 锚定面板右上角实时计算（位置/列数变了跟着走，永远点得到）
QRect BoxAdjustOverlay::checkRectNow() const
{
    const QRect logical = currentLogical();
    return logical.isNull() ? QRect() : QRect(logical.right() - 56, logical.top() + 6, 22, 22);
}

QRect BoxAdjustOverlay::cancelRectNow() const
{
    const QRect logical = currentLogical();
    return logical.isNull() ? QRect() : QRect(logical.right() - 30, logical.top() + 6, 22, 22);
}

int BoxAdjustOverlay::hitZone(const QPoint &localPt) const
{
    const QRect logical = currentLogical();
    if (logical.isNull())
        return 0;
    if (checkRectNow().contains(localPt) || cancelRectNow().contains(localPt))
        return 3; // 按钮
    if (logical.contains(localPt))
        return 1;
    // 宽度取 2×kEdgeGrab：内侧一半先被 zone1（面板内）短路，实际生效热区 =
    // 框外露出的 14px
    const QRect rightEdge(logical.right() - kEdgeGrab, logical.top(), kEdgeGrab + kEdgeGrab,
                          logical.height());
    if (rightEdge.contains(localPt))
        return 2;
    return 0;
}

void BoxAdjustOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter g(this);
    // 全屏暗幕（同时保证整窗不透明、鼠标全域命中；与日历调整层一致）
    g.fillRect(m_screen, QColor(0, 0, 0, 110));
    const QRect logical = currentLogical();
    if (!logical.isNull()) {
        QPen dash(QColor(255, 255, 255, 230), 1.4, Qt::DashLine);
        g.setPen(dash);
        g.setBrush(Qt::NoBrush);
        g.drawRect(logical.adjusted(-2, -2, 2, 2));
        // ✓/× 实时跟随面板右上角
        const QRect check = checkRectNow();
        const QRect cancel = cancelRectNow();
        for (const QRect *r : { &check, &cancel }) {
            QPainterPath p;
            p.addRoundedRect(QRectF(*r), 4, 4);
            g.fillPath(p, QColor(10, 10, 10, 210));
        }
        QPen solid(QColor(255, 255, 255, 255), 2);
        solid.setCapStyle(Qt::RoundCap);
        solid.setJoinStyle(Qt::RoundJoin);
        g.setPen(solid);
        g.setBrush(Qt::NoBrush);
        g.drawPolyline(QVector<QPointF>{ QPointF(check.left() + 5, check.top() + 12),
                                         QPointF(check.left() + 9, check.top() + 16),
                                         QPointF(check.left() + 17, check.top() + 6) });
        g.drawLine(QPointF(cancel.left() + 6, cancel.top() + 6),
                   QPointF(cancel.right() - 6, cancel.bottom() - 6));
        g.drawLine(QPointF(cancel.right() - 6, cancel.top() + 6),
                   QPointF(cancel.left() + 6, cancel.bottom() - 6));
        // 列数提示
        const int cols = BoxLayout::snapCols(logical.width());
        g.setPen(QColor(255, 255, 255, 200));
        QFont f;
        f.setPixelSize(12);
        g.setFont(f);
        g.drawText(QRect(0, m_screen.bottom() - 34, m_screen.width(), 24),
                   Qt::AlignHCenter,
                   QStringLiteral("内部拖动移动位置；拖右缘改列数（当前 %1 列）").arg(cols));
    }
}

// Esc=取消（恢复进入前几何，BoxPanel 侧 original 快照）；Enter=确认。
// T-062 修复: 原 Enter 回写打开时快照 m_startTopLeft/m_startCols——内部拖动已把
// 新几何实时落盘，Enter 一按成果整体回滚，确认键退化成第二个取消键（Enter≡Esc，
// BUG-02）。现与 ✓ 分支对齐：确认 rectProvider 的当前实时几何
void BoxAdjustOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_canceled)
            m_canceled();
        close(); // WA_DeleteOnClose → destroyed → 面板复位 m_adjusting
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        const QRect panel = m_rectProvider ? m_rectProvider() : QRect();
        if (!panel.isNull() && m_confirmed)
            m_confirmed(panel.topLeft(), BoxLayout::snapCols(panel.width()));
        close();
    }
    QWidget::keyPressEvent(event);
}

// ✓/× 热区优先于拖拽热区：✓ 确认 rectProvider 的当前实时几何（拖过的成果），
// × 取消恢复进入前快照；其余按 zone 记录拖拽起点
void BoxAdjustOverlay::mousePressEvent(QMouseEvent *event)
{
    const QPoint local = event->pos();
    if (checkRectNow().contains(local)) {
        const QRect panel = m_rectProvider ? m_rectProvider() : QRect();
        if (!panel.isNull()) {
            if (m_confirmed)
                m_confirmed(panel.topLeft(), BoxLayout::snapCols(panel.width()));
        }
        close();
        return;
    }
    if (cancelRectNow().contains(local)) {
        if (m_canceled)
            m_canceled();
        close();
        return;
    }
    m_zone = hitZone(local);
    m_startLocal = local;
    QWidget::mousePressEvent(event);
}

void BoxAdjustOverlay::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint local = event->pos();
    if (event->buttons() & Qt::LeftButton) {
        if (m_zone == 1) {
            // 内部拖动：平移面板左上角（物理）
            const QRect panel = m_rectProvider ? m_rectProvider() : QRect();
            if (!panel.isNull()) {
                const QPoint delta = local - m_startLocal;
                const QPoint phys(qRound(delta.x() * m_dpr), qRound(delta.y() * m_dpr));
                if (m_changed)
                    m_changed(panel.topLeft() + phys, BoxLayout::snapCols(panel.width()));
                // 拖动基准随面板实时更新，避免反馈回路里越拖越快
                m_startLocal = local;
                update(); // 面板矩形已变——虚线框/✓×必须重画跟随
            }
        } else if (m_zone == 2) {
            // 右缘拖动：改列数（宽度吸附格子倍数，高度随条目自动）
            const QRect panel = m_rectProvider ? m_rectProvider() : QRect();
            if (!panel.isNull()) {
                const int dx = local.x() - m_startLocal.x();
                const int wantWidth = panel.width() + qRound(dx * m_dpr);
                const int cols = BoxLayout::snapCols(wantWidth);
                const int curCols = BoxLayout::snapCols(panel.width());
                if (cols != curCols) {
                    if (m_changed)
                        m_changed(panel.topLeft(), cols);
                    m_startLocal = local;
                } else if ((cols == 8 && dx > 0) || (cols == 2 && dx < 0)) {
                    // T-062 修复: 拖动正被钳制边界吸收（8 列再加宽 / 2 列再收窄）
                    // 时持续重置拖动基准，防 dx 无限累积——否则反向拖回需先补偿
                    // 累积位移才再变列，手感"发粘"（BUG-09）。只在朝界外的方向
                    // 重置：朝界内仍需累积位移过半格阈值才能吸附近邻列
                    m_startLocal = local;
                }
                update();
            }
        }
    } else {
        // 悬停反馈：右缘改手型
        const int zone = hitZone(local);
        setCursor(zone == 2 ? Qt::SizeHorCursor : zone == 1 ? Qt::SizeAllCursor : Qt::ArrowCursor);
    }
    QWidget::mouseMoveEvent(event);
}

// 拖拽结束清 zone；悬停光标由 mouseMove 的无按键分支恢复
void BoxAdjustOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    m_zone = 0;
    QWidget::mouseReleaseEvent(event);
}

} // namespace poptools
