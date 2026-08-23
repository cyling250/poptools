#include "AnnotationEditor.h"

#include "SelectionController.h"

namespace snipaste {

// 标注控制点方块（局部坐标）：线段类端点（P0/P1）经 handlePos 定位
QRectF AnnotationEditor::handleRect(const AnnotationItem &it, Handle h) const
{
    if (h == Handle::None)
        return QRectF();
    const QPointF pt = AnnotationPainter::handlePos(it, h);
    return QRectF(pt - QPointF(kHandle, kHandle), QSizeF(2 * kHandle, 2 * kHandle));
}

// 标注控制点命中（局部坐标）：按类型遍历（矩形类 8 点 / 线段类 2 端点 / 文字无控制点）
Handle AnnotationEditor::handleAt(const AnnotationItem &it, const QPointF &pos) const
{
    for (Handle h : AnnotationPainter::handlesFor(it))
    {
        if (handleRect(it, h).adjusted(-2, -2, 2, 2).contains(pos))
            return h;
    }
    return Handle::None;
}

bool AnnotationEditor::tryPress(const QPointF &local)
{
    Q_ASSERT(m_ctx->canvas);
    Q_ASSERT(m_ctx->selectedItem);
    const int selIdx = m_ctx->selectedIndex();
    if (selIdx >= 0 && selIdx < m_ctx->canvas->items.size())
    {
        const AnnotationItem &sel = m_ctx->canvas->items[selIdx];
        const Handle h = handleAt(sel, local);
        if (h != Handle::None)
        {
            m_itemActiveHandle = h;
            m_itemStartPos = local;
            m_itemStartBounds = AnnotationPainter::itemBounds(sel);
            m_itemStartGeom = sel;
            m_itemSnapshotted = false;
            return true;
        }
        clearSelection(); // 不在选中项控制点上 → 取消选中（下续可能点选新标注）
    }
    // 点选标注项 → 选中并准备拖动
    const int idx = m_ctx->canvas->itemAt(local);
    if (idx >= 0)
    {
        *m_ctx->selectedItem = idx;
        const AnnotationItem &sel = m_ctx->canvas->items[idx];
        m_itemMoving = true;
        m_itemStartPos = local;
        m_itemStartBounds = AnnotationPainter::itemBounds(sel);
        m_itemStartGeom = sel;
        m_itemSnapshotted = false;
        m_ctx->selectionChanged(); // 字号栏等反映新选中项
        m_ctx->requestUpdate();
        return true;
    }
    return false;
}

void AnnotationEditor::drag(const QPointF &local)
{
    if (m_itemActiveHandle != Handle::None)
        resize(local);
    else if (m_itemMoving)
        move(local);
}

void AnnotationEditor::endDrag()
{
    m_itemActiveHandle = Handle::None;
    // m_itemMoving 保持：选中标注保持选中态，便于连续操作
}

void AnnotationEditor::stopInteraction()
{
    m_itemMoving = false;
    m_itemActiveHandle = Handle::None;
}

// 拖动选中项（快照 + 实时替换）
void AnnotationEditor::move(const QPointF &local)
{
    const int selIdx = m_ctx->selectedIndex();
    if (selIdx < 0 || selIdx >= m_ctx->canvas->items.size())
        return;
    const QPointF delta = local - m_itemStartPos;
    if (!m_itemSnapshotted)
    {
        m_ctx->canvas->pushSnapshot(); // 首笔变化才压撤销快照，避免无操作也产生撤销步骤
        m_itemSnapshotted = true;
    }
    AnnotationItem it = m_itemStartGeom;
    it.rect = m_itemStartBounds.translated(delta);
    for (QPointF &pt : it.points)
        pt += delta;
    m_ctx->canvas->setItem(selIdx, it);
    m_ctx->requestUpdate();
}

// 拖动选中项控制点：矩形类镜像缩放（拖过对侧翻转），线段类端点编辑
void AnnotationEditor::resize(const QPointF &local)
{
    const int selIdx = m_ctx->selectedIndex();
    if (selIdx < 0 || selIdx >= m_ctx->canvas->items.size())
        return;
    if (m_itemActiveHandle == Handle::None)
        return;
    const AnnotationItem it = AnnotationPainter::resizedItem(m_itemStartGeom,
                                                             m_itemActiveHandle, local, kMinSize);
    if (!m_itemSnapshotted)
    {
        m_ctx->canvas->pushSnapshot();
        m_itemSnapshotted = true;
    }
    m_ctx->canvas->setItem(selIdx, it);
    m_ctx->requestUpdate();
}

bool AnnotationEditor::tryDoubleClickText(const QPointF &local)
{
    const int idx = m_ctx->canvas->itemAt(local);
    if (idx >= 0 && m_ctx->canvas->items[idx].type == AnnotationType::Text)
    {
        m_ctx->beginTextEdit(local, idx);
        return true;
    }
    return false;
}

void AnnotationEditor::deleteSelected()
{
    const int selIdx = m_ctx->selectedIndex();
    if (selIdx < 0 || selIdx >= m_ctx->canvas->items.size())
        return;
    m_ctx->canvas->pushSnapshot(); // 压撤销栈，撤销可恢复被删项
    m_ctx->canvas->removeItem(selIdx);
    *m_ctx->selectedItem = -1;
    m_itemMoving = false;
    m_itemActiveHandle = Handle::None;
    m_ctx->stateChanged();
    m_ctx->selectionChanged();
    m_ctx->requestUpdate();
}

void AnnotationEditor::clearSelection()
{
    *m_ctx->selectedItem = -1;
    m_itemMoving = false;
    m_itemActiveHandle = Handle::None;
    m_ctx->selectionChanged();
}

void AnnotationEditor::paintSelected(QPainter &p) const
{
    const int selIdx = m_ctx->selectedIndex();
    if (selIdx < 0 || selIdx >= m_ctx->canvas->items.size())
        return;
    const AnnotationItem &selIt = m_ctx->canvas->items[selIdx];
    const QVector<Handle> itemHandles = AnnotationPainter::handlesFor(selIt);
    const bool isSegment = (selIt.type == AnnotationType::Line || selIt.type == AnnotationType::Arrow);
    // 矩形类：外接虚线框 + 控制点；线段类：仅显示起点/终点两个端点控制点
    if (!isSegment)
    {
        const QRectF b = AnnotationPainter::itemBounds(selIt);
        p.setPen(QPen(QColor(0, 200, 120), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(b);
    }
    p.setPen(QPen(QColor(0, 200, 120), 1));
    p.setBrush(Qt::white);
    for (Handle h : itemHandles)
        p.drawRect(handleRect(selIt, h));
}

Qt::CursorShape AnnotationEditor::cursorAt(const QPointF &local) const
{
    const int selIdx = m_ctx->selectedIndex();
    if (selIdx < 0 || selIdx >= m_ctx->canvas->items.size())
        return Qt::ArrowCursor;
    const AnnotationItem &sel = m_ctx->canvas->items[selIdx];
    const Handle h = handleAt(sel, local);
    if (h != Handle::None)
        return SelectionController::cursorShape(h);
    return AnnotationPainter::itemBounds(sel).adjusted(-4, -4, 4, 4).contains(local)
               ? Qt::SizeAllCursor
               : Qt::ArrowCursor;
}

} // namespace snipaste
