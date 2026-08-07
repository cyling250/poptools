#pragma once

#include <QPainter>
#include <QRectF>
#include <QVector>

#include "AnnotationItem.h"

namespace snipaste {

// ============================================================
// 标注渲染与几何工具（纯静态函数，与画板模型解耦）
//
// - 绘制：drawOne / draw（painter 已 translate 到选区左上角）
// - 几何：itemBounds（外接矩形）、scaledItem（等比缩放）、
//   distanceToItem（点到标注最短距离，命中检测用）
// ============================================================
class AnnotationPainter
{
public:
    // 将整组标注绘制到 painter（painter 已 translate 到选区左上角）
    static void draw(QPainter &p, const QVector<AnnotationItem> &items);

    // 绘制单条标注（用于正在绘制的草稿）
    static void drawOne(QPainter &p, const AnnotationItem &it);

    // 标注的外接矩形（选区局部坐标）：Rect/Ellipse/Text/Mosaic 用 rect，折线类用 points
    static QRectF itemBounds(const AnnotationItem &it);

    // 将标注几何从原外接矩形等比变换到 newBounds（拖动缩放用）；
    // 折线顶点按相对坐标映射，文字按高度等比缩放字号
    static AnnotationItem scaledItem(const AnnotationItem &src, const QRectF &newBounds);

    // 计算点到单条标注的最短距离（命中检测用）
    static qreal distanceToItem(const QPointF &pos, const AnnotationItem &it);

private:
    // 计算点到线段的最短距离（QLineF 在 Qt6 中无 distanceToPoint）
    static qreal pointToSegmentDist(const QPointF &p, const QPointF &a, const QPointF &b);
};

} // namespace snipaste
