#include "AnnotationPainter.h"

#include <QLineF>
#include <QtMath>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QPalette>

namespace poptools {

qreal AnnotationPainter::pointToSegmentDist(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal len2 = QPointF::dotProduct(ab, ab);
    if (len2 < 1e-9)
        return QLineF(p, a).length();
    qreal t = QPointF::dotProduct(p - a, ab) / len2;
    t = qBound<qreal>(0, t, 1);
    return QLineF(p, a + ab * t).length();
}

QRectF AnnotationPainter::itemBounds(const AnnotationItem &it)
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

AnnotationItem AnnotationPainter::scaledItem(const AnnotationItem &src, const QRectF &newBounds,
                                             bool xFlip, bool yFlip)
{
    AnnotationItem it = src;
    const QRectF oldBounds = itemBounds(src);
    it.rect = newBounds;
    if (!it.points.isEmpty()) {
        if (oldBounds.width() > 0 && oldBounds.height() > 0) {
            // 常规：按相对坐标等比映射；翻转轴沿新边界反向映射（镜像）
            const qreal kx = newBounds.width() / oldBounds.width();
            const qreal ky = newBounds.height() / oldBounds.height();
            for (QPointF &pt : it.points) {
                qreal nx = newBounds.left() + (pt.x() - oldBounds.left()) * kx;
                if (xFlip)
                    nx = newBounds.right() - (pt.x() - oldBounds.left()) * kx;
                qreal ny = newBounds.top() + (pt.y() - oldBounds.top()) * ky;
                if (yFlip)
                    ny = newBounds.bottom() - (pt.y() - oldBounds.top()) * ky;
                pt = QPointF(nx, ny);
            }
        } else {
            // 退化（水平/垂直直线）：宽度或高度为 0 的轴保持原偏移（翻转轴镜像）
            for (QPointF &pt : it.points) {
                qreal nx = pt.x(), ny = pt.y();
                if (oldBounds.width() > 0) {
                    const qreal kx = newBounds.width() / oldBounds.width();
                    nx = newBounds.left() + (pt.x() - oldBounds.left()) * kx;
                    if (xFlip)
                        nx = newBounds.right() - (pt.x() - oldBounds.left()) * kx;
                } else {
                    nx = xFlip
                             ? newBounds.right() - (pt.x() - oldBounds.left())
                             : newBounds.left() + (pt.x() - oldBounds.left());
                }
                if (oldBounds.height() > 0) {
                    const qreal ky = newBounds.height() / oldBounds.height();
                    ny = newBounds.top() + (pt.y() - oldBounds.top()) * ky;
                    if (yFlip)
                        ny = newBounds.bottom() - (pt.y() - oldBounds.top()) * ky;
                } else {
                    ny = yFlip
                             ? newBounds.bottom() - (pt.y() - oldBounds.top())
                             : newBounds.top() + (pt.y() - oldBounds.top());
                }
                pt = QPointF(nx, ny);
            }
        }
    }
    if (it.type == AnnotationType::Text && oldBounds.height() > 0) {
        // 文字：随框高度等比缩放字号（最小 4pt，避免拖没）
        const qreal s = newBounds.height() / oldBounds.height();
        it.font.setPointSizeF(qMax(4.0, it.font.pointSizeF() * s));
    }
    if (it.type == AnnotationType::Mosaic && !it.mosaicImage.isNull()
        && oldBounds.width() > 0 && oldBounds.height() > 0) {
        // 马赛克图像随框等比缩放，避免多次缩放后质量严重下降
        it.mosaicImage = it.mosaicImage.scaled(
            qMax(1, qRound(it.mosaicImage.width() * newBounds.width() / oldBounds.width())),
            qMax(1, qRound(it.mosaicImage.height() * newBounds.height() / oldBounds.height())),
            Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return it;
}

qreal AnnotationPainter::distanceToItem(const QPointF &pos, const AnnotationItem &it)
{
    qreal d = 1e9;
    switch (it.type) {
    case AnnotationType::Rect:
    case AnnotationType::Ellipse:
    case AnnotationType::Text: {
        // 命中测试：点在图形边界附近（取外接矩形外扩 tol）
        const bool inside = it.rect.adjusted(-6, -6, 6, 6).contains(pos);
        d = inside
                ? qMin(qAbs(pos.x() - it.rect.left()), qMin(qAbs(pos.x() - it.rect.right()),
                                                            qMin(qAbs(pos.y() - it.rect.top()),
                                                                 qAbs(pos.y() - it.rect.bottom()))))
                : 1e9;
        if (inside && d > 6)
            d = 0; // 点在框内（填充/文本区域）
        break;
    }
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

void AnnotationPainter::draw(QPainter &p, const QVector<AnnotationItem> &items)
{
    for (const AnnotationItem &it : items)
        drawOne(p, it);
}

QVector<Handle> AnnotationPainter::handlesFor(const AnnotationItem &it)
{
    switch (it.type) {
    case AnnotationType::Line:
    case AnnotationType::Arrow:
        // 线段类：仅起点 / 终点两个端点控制点（自由改变长度与指向）
        return {Handle::P0, Handle::P1};
    case AnnotationType::Text:
        // 文字：尺寸由内容决定，不许调整长宽 → 不提供缩放控制点
        return {};
    default:
        // 矩形类（Rect/Ellipse/Mosaic/Polyline/Freehand/Doodle）：
        // 8 个角与边中点控制点
        return {Handle::TL, Handle::T, Handle::TR,
                Handle::R,  Handle::BR, Handle::B,
                Handle::BL, Handle::L};
    }
}

QPointF AnnotationPainter::handlePos(const AnnotationItem &it, Handle h)
{
    if (h == Handle::P0)
        return it.points.isEmpty() ? QPointF() : it.points[0];
    if (h == Handle::P1)
        return it.points.size() < 2 ? QPointF() : it.points[1];

    const QRectF r = itemBounds(it);
    const QPointF c = r.center();
    switch (h) {
    case Handle::TL: return r.topLeft();
    case Handle::T:  return QPointF(c.x(), r.top());
    case Handle::TR: return r.topRight();
    case Handle::R:  return QPointF(r.right(), c.y());
    case Handle::BR: return r.bottomRight();
    case Handle::B:  return QPointF(c.x(), r.bottom());
    case Handle::BL: return r.bottomLeft();
    case Handle::L:  return QPointF(r.left(), c.y());
    case Handle::None:
    default:         return QPointF();
    }
}

// 按轴处理"被拖动边"：edge 跟随鼠标；越过对侧起始边时镜像翻转（left/right 互换）。
// 未翻转时该边被钳制在对侧边内侧 minSize 处，翻转后宽度至少 minSize，保证可继续拖。
// 返回该轴是否发生镜像翻转。
static bool resizeAxis(qreal &minEdge, qreal &maxEdge, qreal anchor, qreal edge,
                       bool dragLow, qreal minSize)
{
    if (dragLow) { // 拖动小值边（left/top）
        if (edge < anchor - minSize) {
            minEdge = edge; maxEdge = anchor;                 // 未翻转
            return false;
        }
        minEdge = anchor; maxEdge = qMax(edge, anchor + minSize); // 镜像翻转
        return true;
    }
    // 拖动大值边（right/bottom）
    if (edge > anchor + minSize) {
        minEdge = anchor; maxEdge = edge;                     // 未翻转
        return false;
    }
    minEdge = qMin(edge, anchor - minSize); maxEdge = anchor; // 镜像翻转
    return true;
}

AnnotationItem AnnotationPainter::resizedItem(const AnnotationItem &start, Handle h,
                                              const QPointF &pos, qreal minSize)
{
    // 线段类：端点编辑（只改变该端点，长度与指向自由）
    if (h == Handle::P0 || h == Handle::P1) {
        AnnotationItem it = start;
        const int idx = (h == Handle::P0) ? 0 : 1;
        if (idx < it.points.size())
            it.points[idx] = pos;
        return it;
    }

    // 矩形类：8 控制点镜像缩放（基于起始矩形快照，边随鼠标、越界翻转）
    const QRectF r0 = itemBounds(start);
    qreal left = r0.left(), top = r0.top(), right = r0.right(), bottom = r0.bottom();
    bool xFlip = false, yFlip = false;

    switch (h) {
    case Handle::L: case Handle::TL: case Handle::BL:
        xFlip = resizeAxis(left, right, r0.right(), pos.x(), true, minSize);
        break;
    case Handle::R: case Handle::TR: case Handle::BR:
        xFlip = resizeAxis(left, right, r0.left(), pos.x(), false, minSize);
        break;
    default: break;
    }
    switch (h) {
    case Handle::T: case Handle::TL: case Handle::TR:
        yFlip = resizeAxis(top, bottom, r0.bottom(), pos.y(), true, minSize);
        break;
    case Handle::B: case Handle::BL: case Handle::BR:
        yFlip = resizeAxis(top, bottom, r0.top(), pos.y(), false, minSize);
        break;
    default: break;
    }

    // 边中点（T/B/L/R）只处理单轴，未受控轴保持起始值
    if (h == Handle::T || h == Handle::B) {
        left = r0.left(); right = r0.right();
    } else if (h == Handle::L || h == Handle::R) {
        top = r0.top(); bottom = r0.bottom();
    }

    const QRectF newBounds(left, top, right - left, bottom - top);
    return scaledItem(start, newBounds, xFlip, yFlip);
}

void AnnotationPainter::drawOne(QPainter &p, const AnnotationItem &it)
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
    case AnnotationType::Text: {
        if (it.html.isEmpty()) {
            if (it.inPlaceFit) {
                // OCR 原位拟合：字体行高（ascent+descent）远大于字形实际墨迹
                //（如雅黑行高≈1.33em、中文字形墨迹≈0.97em，按行高拟合会缩小约 27%），
                // 故用 tightBoundingRect 实测墨迹矩形映射到 rect。
                // 纵向精确匹配；横向优先调字距（不使字形变形），
                // 残余差异再以很小的横向缩放兜底（避免明显的横向拉伸感）
                QFont f = it.font;
                const QFontMetricsF fm0(f);
                const QRectF ink0 = fm0.tightBoundingRect(it.text);
                if (ink0.width() > 0 && ink0.height() > 0)
                {
                    const qreal sy = it.rect.height() / ink0.height();
                    const int gaps = qMax(0, it.text.length() - 1);
                    if (gaps > 0)
                    {
                        // 每个字符间隙分摊宽度差，限制在 [-0.1em, 0.35em] 内
                        const qreal em = f.pixelSize() > 0 ? f.pixelSize() : f.pointSizeF();
                        const qreal sp = qBound(-0.1 * em, (it.rect.width() - ink0.width()) / gaps,
                                                0.35 * em);
                        f.setLetterSpacing(QFont::AbsoluteSpacing, sp);
                    }
                    const QRectF ink = QFontMetricsF(f).tightBoundingRect(it.text);
                    // 兜底横向缩放钳制在 ±10% 内；超出部分接受轻微未盖满
                    const qreal sx = qBound(0.9, it.rect.width() / ink.width(), 1.1);
                    // tightBoundingRect 以基线为 y=0；先平移再横向缩放，把墨迹矩形对到 rect
                    p.save();
                    p.setFont(f);
                    p.translate(it.rect.left() - ink.left() * sx, it.rect.top() - ink.top());
                    p.scale(sx, sy);
                    p.drawText(QPointF(0, 0), it.text);
                    p.restore();
                }
            } else {
                // 纯文本（用户文字工具）：单一字体从左上角直接绘制
                p.setFont(it.font);
                p.drawText(it.rect.toRect(), Qt::AlignLeft | Qt::AlignTop, it.text);
            }
        } else {
            // 富文本（含多字号片段）：按与输入框一致的布局渲染
            // （不换行：宽=最长行、高=总行高；body 无颜色 → 用标注色兜底）
            QTextDocument doc;
            doc.setDefaultFont(it.font);
            doc.setHtml(it.html);
            doc.setDocumentMargin(0);
            doc.setTextWidth(-1); // 不换行（与 QTextEdit::NoWrap 输入所见一致）
            QAbstractTextDocumentLayout::PaintContext ctx;
            ctx.palette.setColor(QPalette::Text, it.color);
            p.save();
            p.translate(it.rect.topLeft());
            doc.documentLayout()->draw(&p, ctx);
            p.restore();
        }
        break;
    }
    }
}

} // namespace poptools
