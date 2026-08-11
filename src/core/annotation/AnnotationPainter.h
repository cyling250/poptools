#pragma once

#include <QPainter>
#include <QRectF>
#include <QVector>

#include "AnnotationItem.h"

namespace snipaste {

// 标注/选区的编辑控制点
//   TL/T/TR/R/BR/B/BL/L：矩形类 8 个角与边中点
//   P0/P1：线段类（直线/箭头）的起点 / 终点
enum class Handle {
    None,
    TL, T, TR,   // 上左 / 上中 / 上右
    R, BR, B,    // 右中 / 右下 / 下中
    BL, L,       // 左下 / 左中
    P0, P1       // 线段端点（拖动改变长度与指向）
};

// ============================================================
// 标注渲染与几何工具（纯静态函数，与画板模型解耦）
//
// - 绘制：drawOne / draw（painter 已 translate 到选区左上角）
// - 几何：itemBounds（外接矩形）、scaledItem（等比缩放，支持镜像）、
//   distanceToItem（点到标注最短距离，命中检测用）
// - 编辑：handlesFor / handlePos / resizedItem（按类型提供
//   控制点，矩形类镜像缩放、线段类端点编辑）
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
    // 折线顶点按相对坐标映射，文字按高度等比缩放字号。
    // xFlip / yFlip 表示对应轴发生镜像翻转（控制点拖过对侧边），
    // 此时沿该轴按"新边界反方向"映射，产生 Visio 式镜像缩放。
    static AnnotationItem scaledItem(const AnnotationItem &src, const QRectF &newBounds,
                                     bool xFlip = false, bool yFlip = false);

    // 计算点到单条标注的最短距离（命中检测用）
    static qreal distanceToItem(const QPointF &pos, const AnnotationItem &it);

    // 标注可编辑控制点（按类型返回：矩形类 8 角/边中点，线段类 2 端点）
    static QVector<Handle> handlesFor(const AnnotationItem &it);

    // 控制点当前位置（选区局部坐标）
    static QPointF handlePos(const AnnotationItem &it, Handle h);

    // 拖动控制点 h 到 pos 后的新标注（基于 start 快照）：
    //   - 线段类：P0/P1 直接改端点
    //   - 矩形类：被拖动边跟随鼠标，越过对侧起始边时镜像翻转（尺寸不小于 minSize）
    static AnnotationItem resizedItem(const AnnotationItem &start, Handle h,
                                      const QPointF &pos, qreal minSize);

private:
    // 计算点到线段的最短距离（QLineF 在 Qt6 中无 distanceToPoint）
    static qreal pointToSegmentDist(const QPointF &p, const QPointF &a, const QPointF &b);
};

} // namespace snipaste
