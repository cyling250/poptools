#include "DesktopAdjustOverlay.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

namespace poptools {

namespace {
QRect divRect(const QRect &r, qreal dpr)
{
    return QRect(qRound(r.left() / dpr), qRound(r.top() / dpr), qRound(r.width() / dpr),
                 qRound(r.height() / dpr));
}
QRect mulRect(const QRect &r, qreal dpr)
{
    return QRect(qRound(r.left() * dpr), qRound(r.top() * dpr), qRound(r.width() * dpr),
                 qRound(r.height() * dpr));
}
} // namespace

DesktopAdjustOverlay::DesktopAdjustOverlay(const QRect &calendarRectPhysical,
                                             const QRect &screenPhysical, qreal dpr,
                                             std::function<void(const QRect &)> onChangedPhysical,
                                             std::function<void(const QRect &)> onConfirmedPhysical,
                                             std::function<void()> onCanceled, QWidget *parent)
    : QWidget(parent, Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
    , m_changed(std::move(onChangedPhysical))
    , m_confirmed(std::move(onConfirmedPhysical))
    , m_canceled(std::move(onCanceled))
    , m_dpr(dpr)
    , m_screen(divRect(screenPhysical, dpr))
    , m_inner(divRect(calendarRectPhysical, dpr))
{
    // 物理→逻辑建坐标系（screen/inner ÷dpr），回调统一 mulRect 换回物理；
    // WA_DeleteOnClose 保证 close 即销毁（CalendarPanel 靠 destroyed 复位 m_adjusting）
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    updateBoundsFromInner();
}

// ✓/× 按钮热区：锚定内框右上角实时推导（窗口局部坐标；与 BoxAdjustOverlay
// 同族方案）。不存成员、paint 不回填——拖拽后 update() 异步重绘，paint 未跑
// 前命中也能拿到最新位置（T-062 BUG-07）
QRect DesktopAdjustOverlay::checkRectNow() const
{
    return QRect(m_inner.right() - x() - 66, m_inner.top() - y() + 8, 24, 24);
}

QRect DesktopAdjustOverlay::cancelRectNow() const
{
    return QRect(m_inner.right() - x() - 34, m_inner.top() - y() + 8, 24, 24);
}

void DesktopAdjustOverlay::paintEvent(QPaintEvent *)
{
    // 暗幕（CompositionMode_Source 保半透明正确叠加）+ 内框虚线 + ✓/× 按钮 +
    // 操作提示；✓/× 热区经 checkRectNow/cancelRectNow 实时推导（不在此回填）
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    g.setCompositionMode(QPainter::CompositionMode_Source);
    g.fillRect(rect(), QColor(0, 0, 0, 120));
    g.setCompositionMode(QPainter::CompositionMode_SourceOver);

    const QRect inner(m_inner.left() - x(), m_inner.top() - y(), m_inner.width(), m_inner.height());
    QPen dashPen(QColor(255, 255, 255, 255), 1.3, Qt::DashLine);
    g.setPen(dashPen);
    g.setBrush(Qt::NoBrush);
    g.drawRect(QRectF(inner));

    const QRect checkRect = checkRectNow();
    const QRect cancelRect = cancelRectNow();
    g.setPen(Qt::NoPen);
    g.setBrush(QColor(18, 20, 26, 255));
    g.drawRect(checkRect);
    g.drawRect(cancelRect);
    QPen checkPen(QColor(240, 166, 72), 2);
    checkPen.setCapStyle(Qt::RoundCap);
    checkPen.setJoinStyle(Qt::RoundJoin);
    g.setPen(checkPen);
    g.drawPolyline(QVector<QPoint>{ QPoint(checkRect.left() + 6, checkRect.top() + 12),
                                    QPoint(checkRect.left() + 11, checkRect.top() + 17),
                                    QPoint(checkRect.left() + 19, checkRect.top() + 7) });
    QPen cancelPen(QColor(235, 235, 235), 2);
    cancelPen.setCapStyle(Qt::RoundCap);
    g.setPen(cancelPen);
    g.drawLine(cancelRect.left() + 7, cancelRect.top() + 7, cancelRect.right() - 7,
               cancelRect.bottom() - 7);
    g.drawLine(cancelRect.right() - 7, cancelRect.top() + 7, cancelRect.left() + 7,
               cancelRect.bottom() - 7);

    QFont font;
    font.setFamilies({ QStringLiteral("Microsoft YaHei UI") });
    font.setPixelSize(12);
    g.setFont(font);
    g.setPen(QColor(240, 240, 240));
    g.drawText(QPoint(inner.left() + 4, inner.top() + 22),
               QStringLiteral("拖动边缘调整大小，拖动内部移动位置"));
}

// Esc=取消（回调 + close）；Enter=确认 mulRect(m_inner)（与 ✓ 同源，拖动中
// m_inner 即最新几何）
void DesktopAdjustOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_canceled)
            m_canceled();
        close();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_confirmed)
            m_confirmed(mulRect(m_inner, m_dpr));
        close();
    }
    QWidget::keyPressEvent(event);
}

// 位掩码热区：1=左缘 2=右缘 4=顶缘 8=底缘（各 ±14px），0=内部（移动），
// -1=框外（不响应）；组合位 5/6/9/10 命中对角光标
int DesktopAdjustOverlay::hitZone(const QPoint &localPt) const
{
    // rlqd 以屏幕坐标做边缘判定；此处等效为窗口局部坐标
    constexpr int edge = 14;
    int zone = 0;
    if (std::abs(localPt.x() - (m_inner.left() - x())) <= edge)
        zone |= 1;
    if (std::abs(localPt.x() - (m_inner.left() - x() + m_inner.width())) <= edge)
        zone |= 2;
    if (std::abs(localPt.y() - (m_inner.top() - y())) <= edge)
        zone |= 4;
    if (std::abs(localPt.y() - (m_inner.top() - y() + m_inner.height())) <= edge)
        zone |= 8;
    if (zone != 0)
        return zone;
    const QRect innerLocal(m_inner.left() - x(), m_inner.top() - y(), m_inner.width(),
                           m_inner.height());
    return innerLocal.contains(localPt) ? 0 : -1;
}

// 逻辑 → 物理（mulRect 同义别名，保留只为调用点可读）
QRect DesktopAdjustOverlay::toPhysical(const QRect &logical) const
{
    return mulRect(logical, m_dpr);
}

// ✓/× 热区优先于拖拽；其后按 hitZone 记拖拽起点并 grabMouse（拖出窗口仍续）
void DesktopAdjustOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (checkRectNow().contains(event->pos())) {
        if (m_confirmed)
            m_confirmed(mulRect(m_inner, m_dpr));
        close();
        return;
    }
    if (cancelRectNow().contains(event->pos())) {
        if (m_canceled)
            m_canceled();
        close();
        return;
    }
    const int z = hitZone(event->pos());
    if (z < 0)
        return;
    m_zone = z;
    m_startRect = m_inner;
    m_startLocal = event->pos();
    m_dragging = true;
    grabMouse();
}

// 未拖拽按热区换光标（组合位 5/6/9/10=对角）；拖拽中 zone==0 整体平移、其余
// 按位单/双边缩放：先 min 尺寸、再钳屏、最后补 min
void DesktopAdjustOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        const int z = hitZone(event->pos());
        switch (z) {
            case 1:
            case 2:
                setCursor(Qt::SizeHorCursor);
                break;
            case 4:
            case 8:
                setCursor(Qt::SizeVerCursor);
                break;
            case 5:
            case 10:
                setCursor(Qt::SizeFDiagCursor);
                break;
            case 6:
            case 9:
                setCursor(Qt::SizeBDiagCursor);
                break;
            case 0:
                setCursor(Qt::SizeAllCursor);
                break;
            default:
                unsetCursor();
                break;
        }
        return;
    }
    const QPoint d = event->pos() - m_startLocal;
    int l = m_startRect.left();
    int t = m_startRect.top();
    int r = m_startRect.left() + m_startRect.width();
    int b = m_startRect.top() + m_startRect.height();
    if (m_zone == 0) {
        l += d.x(); r += d.x(); t += d.y(); b += d.y();
    } else {
        if (m_zone & 1) l += d.x();
        if (m_zone & 2) r += d.x();
        if (m_zone & 4) t += d.y();
        if (m_zone & 8) b += d.y();
    }
    constexpr int minW = 520, minH = 420;
    if (r - l < minW) {
        if (m_zone & 1) l = r - minW;
        else r = l + minW;
    }
    if (b - t < minH) {
        if (m_zone & 4) t = b - minH;
        else b = t + minH;
    }
    l = std::max(m_screen.left(), l);
    t = std::max(m_screen.top(), t);
    r = std::min(m_screen.left() + m_screen.width(), r);
    b = std::min(m_screen.top() + m_screen.height(), b);
    if (r - l < minW)
        r = std::min(m_screen.left() + m_screen.width(), l + minW);
    if (b - t < minH)
        b = std::min(m_screen.top() + m_screen.height(), t + minH);
    m_inner = QRect(l, t, r - l, b - t);
    updateBoundsFromInner();
    if (m_changed)
        m_changed(toPhysical(m_inner));
}

// 结束拖拽并补发一次 changed（松手位置为终值）
void DesktopAdjustOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    if (m_dragging) {
        m_dragging = false;
        releaseMouse();
        if (m_changed)
            m_changed(toPhysical(m_inner));
    }
}

// 窗口几何 = 内框 ±26px 边距并钳到屏内（内框必须完整落在窗内，hitZone 的局部
// 坐标换算才成立）
void DesktopAdjustOverlay::updateBoundsFromInner()
{
    constexpr int margin = 26;
    QRect bounds(m_inner.left() - margin, m_inner.top() - margin, m_inner.width() + margin * 2,
                 m_inner.height() + margin * 2);
    bounds = bounds.intersected(m_screen);
    setGeometry(bounds);
    update();
}

} // namespace poptools
