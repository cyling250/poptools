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
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    updateBoundsFromInner();
}

void DesktopAdjustOverlay::paintEvent(QPaintEvent *)
{
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

    m_checkRect = QRect(inner.right() - 66, inner.top() + 8, 24, 24);
    m_cancelRect = QRect(inner.right() - 34, inner.top() + 8, 24, 24);
    g.setPen(Qt::NoPen);
    g.setBrush(QColor(18, 20, 26, 255));
    g.drawRect(m_checkRect);
    g.drawRect(m_cancelRect);
    QPen checkPen(QColor(240, 166, 72), 2);
    checkPen.setCapStyle(Qt::RoundCap);
    checkPen.setJoinStyle(Qt::RoundJoin);
    g.setPen(checkPen);
    g.drawPolyline(QVector<QPoint>{ QPoint(m_checkRect.left() + 6, m_checkRect.top() + 12),
                                    QPoint(m_checkRect.left() + 11, m_checkRect.top() + 17),
                                    QPoint(m_checkRect.left() + 19, m_checkRect.top() + 7) });
    QPen cancelPen(QColor(235, 235, 235), 2);
    cancelPen.setCapStyle(Qt::RoundCap);
    g.setPen(cancelPen);
    g.drawLine(m_cancelRect.left() + 7, m_cancelRect.top() + 7, m_cancelRect.right() - 7,
               m_cancelRect.bottom() - 7);
    g.drawLine(m_cancelRect.right() - 7, m_cancelRect.top() + 7, m_cancelRect.left() + 7,
               m_cancelRect.bottom() - 7);

    QFont font;
    font.setFamilies({ QStringLiteral("Microsoft YaHei UI") });
    font.setPixelSize(12);
    g.setFont(font);
    g.setPen(QColor(240, 240, 240));
    g.drawText(QPoint(inner.left() + 4, inner.top() + 22),
               QStringLiteral("拖动边缘调整大小，拖动内部移动位置"));
}

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

QRect DesktopAdjustOverlay::toPhysical(const QRect &logical) const
{
    return mulRect(logical, m_dpr);
}

void DesktopAdjustOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (m_checkRect.contains(event->pos())) {
        if (m_confirmed)
            m_confirmed(mulRect(m_inner, m_dpr));
        close();
        return;
    }
    if (m_cancelRect.contains(event->pos())) {
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
