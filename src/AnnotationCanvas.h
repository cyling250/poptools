#pragma once

#include <QColor>
#include <QRectF>
#include <QPolygonF>
#include <QVector>
#include <QString>
#include <QFont>
#include <QImage>
#include <QPainter>

// ============================================================
// 画板工具类：管理截图上的标注内容
//
// - 标注项类型：矩形 / 椭圆 / 直线 / 箭头 / 折线 / 铅笔 /
//   涂鸦笔 / 马赛克 / 文本
// - 撤销 / 重做（基于标注项列表快照）
// - 橡皮擦：命中检测删除标注
// - 当前工具、颜色、线宽、填充状态
//
// 坐标约定：标注项使用"选区局部坐标"（相对选区左上角），
// 选区移动时标注随选区整体平移，绘制时由调用方 translate。
// ============================================================

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

// 单条标注项
struct AnnotationItem
{
    AnnotationType type = AnnotationType::Rect;
    QColor color = Qt::red;
    qreal width = 2.0;
    bool filled = false;      // 矩形/椭圆是否填充
    QRectF rect;              // Rect/Ellipse/Text 的外接矩形（局部坐标）
    QPolygonF points;         // Line/Arrow/Polyline/Freehand/Doodle 顶点（局部坐标）
    QString text;             // Text 内容
    QFont font;               // Text 字体
    QImage mosaicImage;       // Mosaic 的马赛克图像（局部坐标区域）
};

// 画板：标注集合 + 撤销/重做 + 工具状态
class AnnotationCanvas
{
public:
    // 当前工具（由 CaptureWindow 切换）
    enum class Tool {
        None,        // 选择模式：可拖动画布
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

    QVector<AnnotationItem> items;   // 当前标注列表
    QColor color = Qt::red;          // 当前画笔/边框颜色
    qreal width = 2.0;               // 当前线宽
    bool filled = false;             // 矩形/椭圆是否填充
    Tool tool = Tool::Rect;          // 当前工具

    void addItem(const AnnotationItem &item);   // 添加标注（入撤销栈）
    void undo();
    void redo();
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }

    // 橡皮擦：删除与 pos 距离最近的标注，返回是否删除了
    bool eraseAt(const QPointF &pos, qreal tol = 6.0);

    // 判断某点是否落在任一标注上（用于拖动画布时避开标注）
    bool hitTest(const QPointF &pos, qreal tol = 6.0) const;

    // 从后往前返回命中 pos 的标注索引（选择模式点选用），未命中返回 -1
    int itemAt(const QPointF &pos, qreal tol = 6.0) const;

    // 交互式修改支持（调用方先 pushSnapshot，再多次 setItem/removeItem 实时更新，
    // 避免拖拽过程中重复压撤销栈；pushSnapshot 后首次撤销即回到修改前状态）
    void pushSnapshot();                                  // 记录当前 items 快照（实际修改前调用一次）
    void setItem(int index, const AnnotationItem &item);  // 原位替换（不压栈）
    void removeItem(int index);                           // 删除指定索引（不压栈）

    // 标注的外接矩形（选区局部坐标）：Rect/Ellipse/Text/Mosaic 用 rect，折线类用 points
    static QRectF itemBounds(const AnnotationItem &it);

    // 将标注几何从原外接矩形等比变换到 newBounds（拖动缩放用）；
    // 折线顶点按相对坐标映射，文字按高度等比缩放字号
    static AnnotationItem scaledItem(const AnnotationItem &src, const QRectF &newBounds);

    // 计算点到单条标注的最短距离（命中检测用）
    static qreal distanceToItem(const QPointF &pos, const AnnotationItem &it);

    // 清空所有标注（重新截图时调用）
    void clear();

    // 将标注绘制到 painter（painter 已 translate 到选区左上角）
    void draw(QPainter &p) const;

    // 绘制单条标注（用于正在绘制的草稿）
    static void drawOne(QPainter &p, const AnnotationItem &it);

private:
    QVector<QVector<AnnotationItem>> m_undoStack; // 历史快照
    QVector<QVector<AnnotationItem>> m_redoStack;
};
