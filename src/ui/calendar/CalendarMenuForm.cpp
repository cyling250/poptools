#include "CalendarMenuForm.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace poptools {

namespace {
constexpr int kItemHeight = 30;
constexpr int kPad = 8;
constexpr int kSep = 7;
constexpr int kWidth = 200;

QPainterPath menuRoundPath(const QRectF &r, qreal radius)
{
    QPainterPath path;
    const qreal d = std::min(radius * 2, std::min(r.width(), r.height()));
    path.moveTo(r.left(), r.top() + d);
    path.arcTo(QRectF(r.left(), r.top(), d, d), 180, -90);
    path.lineTo(r.right() - d, r.top());
    path.arcTo(QRectF(r.right() - d, r.top(), d, d), 90, -90);
    path.lineTo(r.right(), r.bottom() - d);
    path.arcTo(QRectF(r.right() - d, r.bottom() - d, d, d), 0, -90);
    path.lineTo(r.left() + d, r.bottom());
    path.arcTo(QRectF(r.left(), r.bottom() - d, d, d), 270, -90);
    path.closeSubpath();
    return path;
}
} // namespace

CalendarMenuForm::CalendarMenuForm(std::vector<CalendarMenuItemDef> items, const QPoint &pos,
                                   QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
    , m_items(std::move(items))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    int h = kPad * 2;
    for (const auto &it : m_items)
        h += it.separatorAfter ? kItemHeight + kSep : kItemHeight;
    resize(kWidth, h);
    move(pos);
}

void CalendarMenuForm::paintEvent(QPaintEvent *)
{
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    g.setCompositionMode(QPainter::CompositionMode_Source);
    g.fillRect(rect(), QColor(26, 28, 34));
    g.setCompositionMode(QPainter::CompositionMode_SourceOver);
    g.setPen(QPen(QColor(255, 255, 255, 60), 1));
    g.setBrush(Qt::NoBrush);
    g.drawPath(menuRoundPath(QRectF(rect()).adjusted(0, 0, -1, -1), 8));

    m_rects.clear();
    int y = kPad;
    QFont font;
    font.setFamilies({ QStringLiteral("Microsoft YaHei UI") });
    font.setPixelSize(12);
    g.setFont(font);
    for (size_t i = 0; i < m_items.size(); ++i) {
        const auto &it = m_items[i];
        const QRect r(4, y, width() - 8, kItemHeight);
        m_rects.push_back(r);
        if (int(i) == m_hover)
            g.fillPath(menuRoundPath(QRectF(r), 5), QColor(255, 255, 255, 50));
        if (it.showCheck) {
            QPen checkPen(QColor(240, 166, 72, 230), 1.6);
            g.setPen(checkPen);
            g.setBrush(Qt::NoBrush);
            const QRect cr(r.left() + 8, r.top() + 9, 12, 12);
            g.drawRect(cr);
            if (it.checked) {
                g.drawPolyline(QVector<QPoint>{ QPoint(cr.left() + 3, cr.top() + 6),
                                                QPoint(cr.left() + 6, cr.top() + 9),
                                                QPoint(cr.left() + 10, cr.top() + 3) });
            }
            g.setPen(Qt::NoPen);
        }
        g.setPen(QColor(238, 238, 238));
        g.drawText(QRect(r.left() + (it.showCheck ? 28 : 12), r.top() + 7, r.width(), kItemHeight),
                   Qt::AlignLeft | Qt::AlignTop, it.text);
        y += kItemHeight;
        if (it.separatorAfter) {
            g.setPen(QPen(QColor(255, 255, 255, 34), 1));
            g.drawLine(12, y + 2, width() - 12, y + 2);
            y += kSep;
        }
    }
}

int CalendarMenuForm::hit(const QPoint &p) const
{
    for (size_t i = 0; i < m_rects.size(); ++i)
        if (m_rects[i].contains(p))
            return int(i);
    return -1;
}

void CalendarMenuForm::mouseMoveEvent(QMouseEvent *event)
{
    const int h = hit(event->pos());
    if (h != m_hover) {
        m_hover = h;
        update();
    }
}

void CalendarMenuForm::mouseLeaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

void CalendarMenuForm::mousePressEvent(QMouseEvent *event)
{
    const int idx = hit(event->pos());
    if (idx >= 0) {
        const auto action = m_items[size_t(idx)].action;
        close();
        if (action)
            action();
    }
}

void CalendarMenuForm::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        close();
    QWidget::keyPressEvent(event);
}

bool CalendarMenuForm::event(QEvent *event)
{
    if (event->type() == QEvent::Leave)
        mouseLeaveEvent(event);
    if (event->type() == QEvent::WindowDeactivate)
        close();
    return QWidget::event(event);
}

} // namespace poptools
