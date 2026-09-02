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
    QString text;             // Text 内容（纯文本）
    QFont font;               // Text 默认字体（family / bold / 默认字号）
    QString html;             // Text 富文本内容（含多字号片段；空 = 纯文本渲染，兼容旧数据）
    bool inPlaceFit = false;  // OCR 原位拟合：渲染时把文字墨迹矩形精确映射到 rect（盖住所识别的原文字）
    QImage mosaicImage;       // Mosaic 的马赛克图像（局部坐标区域）
};

} // namespace snipaste
