#include "AnnotationCanvas.h"
#include "AnnotationPainter.h"

namespace poptools {

void AnnotationCanvas::addItem(const AnnotationItem &item)
{
    pushUndo(items);
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
    pushUndo(items);
    items = m_redoStack.takeLast();
}

bool AnnotationCanvas::eraseAt(const QPointF &pos, qreal tol)
{
    // 从后往前（后绘制优先 = 最上层）找第一个容差内的标注，命中即删——
    // 不是"距离最近"的那条
    for (int i = items.size() - 1; i >= 0; --i) {
        const AnnotationItem &it = items[i];
        qreal d = AnnotationPainter::distanceToItem(pos, it, tol);
        if (d <= tol) {
            pushUndo(items);
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
        if (AnnotationPainter::distanceToItem(pos, it, tol) <= tol)
            return true;
    }
    return false;
}

int AnnotationCanvas::itemAt(const QPointF &pos, qreal tol) const
{
    // 从后往前：后绘制的标注优先被选中
    for (int i = items.size() - 1; i >= 0; --i) {
        if (AnnotationPainter::distanceToItem(pos, items[i], tol) <= tol)
            return i;
    }
    return -1;
}

void AnnotationCanvas::pushSnapshot()
{
    pushUndo(items);
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

void AnnotationCanvas::clear()
{
    items.clear();
    m_undoStack.clear();
    m_redoStack.clear();
}

} // namespace poptools
