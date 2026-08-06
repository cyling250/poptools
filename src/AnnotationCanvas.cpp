#include "AnnotationCanvas.h"

#include <QLineF>
#include <QtMath>

// 计算点到线段的最短距离（QLineF 在 Qt6 中无 distanceToPoint）
static qreal pointToSegmentDist(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal len2 = QPointF::dotProduct(ab, ab);
    if (len2 < 1e-9)
        return QLineF(p, a).length();
    qreal t = QPointF::dotProduct(p - a, ab) / len2;
    t = qBound<qreal>(0, t, 1);
    return QLineF(p, a + ab * t).length();
}

void AnnotationCanvas::addItem(const AnnotationItem &item)
{
    m_undoStack.push_back(items);
    items.push_back(item);
    m_redoStack.clear();
}

void AnnotationCanvas::undo()
{
    if (m_undoStack.isEmpty())
        return;
    m_redoStack.push_back(items);
    items = m_undoStack.takeLast();
}

void AnnotationCanvas::redo()
{
    if (m_redoStack.isEmpty())
        return;
    m_undoStack.push_back(items);
    items = m_redoStack.takeLast();
}

bool AnnotationCanvas::eraseAt(const QPointF &pos, qreal tol)
{
    // 从后往前找最近且距离在容差内的标注
    for (int i = items.size() - 1; i >= 0; --i) {
        const AnnotationItem &it = items[i];
        qreal d = 1e9;
        switch (it.type) {
        case AnnotationType::Rect:
        case AnnotationType::Ellipse:
        case AnnotationType::Text:
            // 命中测试：点在图形边界附近（取外接矩形外扩 tol）
            d = it.rect.adjusted(-tol, -tol, tol, tol).contains(pos)
                    ? qMin(qAbs(pos.x() - it.rect.left()), qMin(qAbs(pos.x() - it.rect.right()),
                                                                qMin(qAbs(pos.y() - it.rect.top()),
                                                                     qAbs(pos.y() - it.rect.bottom()))))
                    : 1e9;
            if (it.rect.adjusted(-tol, -tol, tol, tol).contains(pos) && d > tol)
                d = 0; // 点在框内（填充/文本区域）
            break;
        case AnnotationType::Arrow:
        case AnnotationType::Polyline:
        case AnnotationType::Freehand: {
            // 到折线的最小距离
            for (int j = 1; j < it.points.size(); ++j)
                d = qMin(d, pointToSegmentDist(pos, it.points[j - 1], it.points[j]));
            if (it.points.size() == 1)
                d = pointToSegmentDist(pos, it.points[0], it.points[0]);
            break;
        }
        }
        if (d <= tol) {
            m_undoStack.push_back(items);
            items.removeAt(i);
            m_redoStack.clear();
            return true;
        }
    }
    return false;
}

void AnnotationCanvas::clear()
{
    items.clear();
    m_undoStack.clear();
    m_redoStack.clear();
}

void AnnotationCanvas::draw(QPainter &p) const
{
    for (const AnnotationItem &it : items)
        drawOne(p, it);
}

void AnnotationCanvas::drawOne(QPainter &p, const AnnotationItem &it)
{
    p.setPen(QPen(it.color, it.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(it.filled ? it.color : Qt::NoBrush);
    switch (it.type) {
    case AnnotationType::Rect:
        p.drawRect(it.rect);
        break;
    case AnnotationType::Ellipse:
        p.drawEllipse(it.rect);
        break;
    case AnnotationType::Arrow: {
        if (it.points.size() < 2)
            break;
        const QLineF line(it.points[0], it.points[1]);
        p.drawLine(line);
        // 箭头头部
        const qreal ang = std::atan2(-line.dy(), line.dx());
        const qreal headLen = qMax(8.0, it.width * 3.0);
        const QPointF p1 = line.p2() - QPointF(headLen * std::cos(ang - M_PI / 6),
                                               -headLen * std::sin(ang - M_PI / 6));
        const QPointF p2 = line.p2() - QPointF(headLen * std::cos(ang + M_PI / 6),
                                               -headLen * std::sin(ang + M_PI / 6));
        p.setBrush(it.color);
        QPolygonF head;
        head << line.p2() << p1 << p2;
        p.drawPolygon(head);
        break;
    }
    case AnnotationType::Polyline:
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(it.points);
        break;
    case AnnotationType::Freehand:
        p.drawPolyline(it.points);
        break;
    case AnnotationType::Text:
        p.setFont(it.font);
        p.drawText(it.rect.toRect(), Qt::AlignLeft | Qt::AlignTop, it.text);
        break;
    }
}
