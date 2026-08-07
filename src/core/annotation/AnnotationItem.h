#pragma once

#include <QColor>
#include <QRectF>
#include <QPolygonF>
#include <QString>
#include <QFont>
#include <QImage>

#include "AnnotationTypes.h"

namespace snipaste {

// 单条标注项
//
// 坐标约定：标注项使用"选区局部坐标"（相对选区左上角），
// 选区移动时标注随选区整体平移，绘制时由调用方 translate。
struct AnnotationItem
{
    AnnotationType type = AnnotationType::Rect;
    QColor color = Qt::red;
    qreal width = 2.0;
    bool filled = false;      // 矩形/椭圆是否填充
    QRectF rect;              // Rect/Ellipse/Text/Mosaic 的外接矩形（局部坐标）
    QPolygonF points;         // Line/Arrow/Polyline/Freehand/Doodle 顶点（局部坐标）
    QString text;             // Text 内容
    QFont font;               // Text 字体
    QImage mosaicImage;       // Mosaic 的马赛克图像（局部坐标区域）
};

} // namespace snipaste
