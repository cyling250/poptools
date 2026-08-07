#pragma once

#include <QColor>
#include <QVector>

#include "AnnotationItem.h"
#include "AnnotationTypes.h"

namespace snipaste {

// ============================================================
// 标注画板模型：标注集合 + 撤销/重做 + 工具状态
//
// 职责：
//   - 标注项的增删改（撤销/重做基于标注项列表快照）
//   - 橡皮擦命中检测删除
//   - 当前工具、颜色、线宽、填充状态
//   - 命中测试（点选 / 拖动画布避开标注）
//
// 渲染与几何工具（绘制、外接矩形、等比缩放、点到标注距离）
// 见 AnnotationPainter，本类保持纯数据模型。
// ============================================================
class AnnotationCanvas
{
public:
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

    // 清空所有标注（重新截图时调用）
    void clear();

private:
    QVector<QVector<AnnotationItem>> m_undoStack; // 历史快照
    QVector<QVector<AnnotationItem>> m_redoStack;
};

} // namespace snipaste
