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
        qreal d = distanceToItem(pos, it);
        if (d <= tol) {
            m_undoStack.push_back(items);
            items.removeAt(i);
            m_redoStack.clear();
            return true;
        }
    }
    return false;
}

bool AnnotationCanvas::hitTest(const QPointF &pos, qreal tol) const
{
    for (const AnnotationItem &it : items) {
        if (distanceToItem(pos, it) <= tol)
            return true;
    }
    return false;
}

int AnnotationCanvas::itemAt(const QPointF &pos, qreal tol) const
{
    // 从后往前：后绘制的标注优先被选中
    for (int i = items.size() - 1; i >= 0; --i) {
        if (distanceToItem(pos, items[i]) <= tol)
            return i;
    }
    return -1;
}

void AnnotationCanvas::pushSnapshot()
{
    m_undoStack.push_back(items);
    m_redoStack.clear();
}

void AnnotationCanvas::setItem(int index, const AnnotationItem &item)
{
    if (index >= 0 && index < items.size())
        items[index] = item;
}

void AnnotationCanvas::removeItem(int index)
{
    if (index >= 0 && index < items.size())
        items.removeAt(index);
}

QRectF AnnotationCanvas::itemBounds(const AnnotationItem &it)
{
    switch (it.type) {
    case AnnotationType::Rect:
    case AnnotationType::Ellipse:
    case AnnotationType::Text:
    case AnnotationType::Mosaic:
        return it.rect;
    default:
        return it.points.boundingRect();
    }
}

AnnotationItem AnnotationCanvas::scaledItem(const AnnotationItem &src, const QRectF &newBounds)
{
    AnnotationItem it = src;
    const QRectF oldBounds = itemBounds(src);
    it.rect = newBounds;
    if (!it.points.isEmpty()) {
        if (oldBounds.width() > 0 && oldBounds.height() > 0) {
            // 常规：按相对坐标等比映射
            const qreal kx = newBounds.width() / oldBounds.width();
            const qreal ky = newBounds.height() / oldBounds.height();
            for (QPointF &pt : it.points) {
                pt = QPointF(newBounds.left() + (pt.x() - oldBounds.left()) * kx,
                             newBounds.top() + (pt.y() - oldBounds.top()) * ky);
            }
        } else {
            // 退化（水平/垂直直线）：宽度或高度为 0 的轴保持原偏移
            for (QPointF &pt : it.points) {
                qreal nx = pt.x(), ny = pt.y();
                if (oldBounds.width() > 0)
                    nx = newBounds.left()
                         + (pt.x() - oldBounds.left()) * (newBounds.width() / oldBounds.width());
                else
                    nx = newBounds.left() + (pt.x() - oldBounds.left());
                if (oldBounds.height() > 0)
                    ny = newBounds.top()
                         + (pt.y() - oldBounds.top()) * (newBounds.height() / oldBounds.height());
                else
                    ny = newBounds.top() + (pt.y() - oldBounds.top());
                pt = QPointF(nx, ny);
            }
        }
    }
    if (it.type == AnnotationType::Text && oldBounds.height() > 0) {
        // 文字：随框高度等比缩放字号（最小 4pt，避免拖没）
        const qreal s = newBounds.height() / oldBounds.height();
        it.font.setPointSizeF(qMax(4.0, it.font.pointSizeF() * s));
    }
    return it;
}

// 计算点到标注的距离（公有静态内部工具函数）
qreal AnnotationCanvas::distanceToItem(const QPointF &pos, const AnnotationItem &it)
{
    qreal d = 1e9;
    switch (it.type) {
    case AnnotationType::Rect:
    case AnnotationType::Ellipse:
    case AnnotationType::Text:
        // 命中测试：点在图形边界附近（取外接矩形外扩 tol）
        d = it.rect.adjusted(-6, -6, 6, 6).contains(pos)
                ? qMin(qAbs(pos.x() - it.rect.left()), qMin(qAbs(pos.x() - it.rect.right()),
                                                            qMin(qAbs(pos.y() - it.rect.top()),
                                                                 qAbs(pos.y() - it.rect.bottom()))))
                : 1e9;
        if (it.rect.adjusted(-6, -6, 6, 6).contains(pos) && d > 6)
            d = 0; // 点在框内（填充/文本区域）
        break;
    case AnnotationType::Line:
    case AnnotationType::Arrow:
    case AnnotationType::Polyline:
    case AnnotationType::Freehand:
    case AnnotationType::Doodle:
    case AnnotationType::Mosaic: { // 马赛克为笔刷涂抹，按笔迹折线命中
        // 到折线的最小距离
        for (int j = 1; j < it.points.size(); ++j)
            d = qMin(d, pointToSegmentDist(pos, it.points[j - 1], it.points[j]));
        if (it.points.size() == 1)
            d = pointToSegmentDist(pos, it.points[0], it.points[0]);
        break;
    }
    }
    return d;
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
    // 注意：不能写 `it.filled ? it.color : Qt::NoBrush` —— 三元运算符会把
    // Qt::NoBrush(0) 隐式转成 QColor(0) 纯黑画刷，导致未填充矩形内部变黑
    p.setBrush(it.filled ? QBrush(it.color) : QBrush(Qt::NoBrush));
    switch (it.type) {
    case AnnotationType::Rect:
        p.drawRect(it.rect);
        break;
    case AnnotationType::Ellipse:
        p.drawEllipse(it.rect);
        break;
    case AnnotationType::Line:
        if (it.points.size() >= 2)
            p.drawLine(it.points[0], it.points[1]);
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
    case AnnotationType::Doodle:
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(it.points);
        break;
    case AnnotationType::Freehand:
        p.drawPolyline(it.points);
        break;
    case AnnotationType::Mosaic:
        // mosaicImage 为物理像素图，等比缩放到逻辑矩形
        // （画布为逻辑坐标，输出时整体按 dpr 放大，二者一致）
        if (!it.mosaicImage.isNull())
            p.drawImage(it.rect, it.mosaicImage);
        break;
    case AnnotationType::Text:
        p.setFont(it.font);
        p.drawText(it.rect.toRect(), Qt::AlignLeft | Qt::AlignTop, it.text);
        break;
    }
}
