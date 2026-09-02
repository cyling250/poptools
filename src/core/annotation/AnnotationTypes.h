#pragma once

namespace poptools {

// 可绘制的标注类型
enum class AnnotationType {
    Rect,     // 矩形 / 圆角矩形
    Ellipse,  // 椭圆
    Line,     // 直线
    Arrow,    // 箭头
    Polyline, // 折线 / 多边形
    Freehand, // 铅笔 / 自由画笔
    Doodle,   // 涂鸦笔
    Mosaic,   // 马赛克（区域像素化）
    Text,     // 文本
};

// 画布当前交互工具（None = 选择模式，Eraser 为删除操作非绘制）
enum class Tool {
    None,        // 选择模式：可拖动画布 / 标注项
    Rect,        // 矩形
    Ellipse,     // 椭圆
    Line,        // 直线
    Arrow,       // 箭头
    Pencil,      // 铅笔
    Doodle,      // 涂鸦笔
    Mosaic,      // 马赛克
    Text,        // 文本
    Eraser,      // 橡皮擦
};

} // namespace poptools
