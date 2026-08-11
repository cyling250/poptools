#include "SelectionController.h"

#include <QPainterPath>

namespace snipaste {

// ---- 几何（纯函数） ----

QRectF SelectionController::handleRect(const QRectF &r, Handle h)
{
    const QPointF c = r.center();
    QPointF pt;
    switch (h)
    {
    case Handle::TL: pt = r.topLeft(); break;
    case Handle::T:  pt = QPointF(c.x(), r.top()); break;
    case Handle::TR: pt = r.topRight(); break;
    case Handle::R:  pt = QPointF(r.right(), c.y()); break;
    case Handle::BR: pt = r.bottomRight(); break;
    case Handle::B:  pt = QPointF(c.x(), r.bottom()); break;
    case Handle::BL: pt = r.bottomLeft(); break;
    case Handle::L:  pt = QPointF(r.left(), c.y()); break;
    case Handle::None:
    case Handle::P0:
    case Handle::P1:
        return QRectF(); // 端点控制点仅通过标注定位
    }
    return QRectF(pt - QPointF(kHandle, kHandle), QSizeF(2 * kHandle, 2 * kHandle));
}

Handle SelectionController::handleAt(const QRectF &b, const QPointF &pos)
{
    const Handle handles[] = {
        Handle::TL, Handle::T, Handle::TR,
        Handle::R, Handle::BR, Handle::B,
        Handle::BL, Handle::L};
    for (Handle h : handles)
    {
        if (handleRect(b, h).adjusted(-2, -2, 2, 2).contains(pos))
            return h;
    }
    return Handle::None;
}

QRectF SelectionController::resizeRectF(QRectF r, Handle h, const QPointF &pos, qreal minSize)
{
    switch (h)
    {
    case Handle::TL:
        r.setTopLeft(QPointF(qMin(pos.x(), r.right() - minSize),
                             qMin(pos.y(), r.bottom() - minSize)));
        break;
    case Handle::T:
        r.setTop(qMin(pos.y(), r.bottom() - minSize));
        break;
    case Handle::TR:
        r.setTopRight(QPointF(qMax(pos.x(), r.left() + minSize),
                              qMin(pos.y(), r.bottom() - minSize)));
        break;
    case Handle::R:
        r.setRight(qMax(pos.x(), r.left() + minSize));
        break;
    case Handle::BR:
        r.setBottomRight(QPointF(qMax(pos.x(), r.left() + minSize),
                                 qMax(pos.y(), r.top() + minSize)));
        break;
    case Handle::B:
        r.setBottom(qMax(pos.y(), r.top() + minSize));
        break;
    case Handle::BL:
        r.setBottomLeft(QPointF(qMin(pos.x(), r.right() - minSize),
                                qMax(pos.y(), r.top() + minSize)));
        break;
    case Handle::L:
        r.setLeft(qMin(pos.x(), r.right() - minSize));
        break;
    case Handle::None:
        break;
    }
    return r;
}

Qt::CursorShape SelectionController::cursorShape(Handle h)
{
    switch (h)
    {
    case Handle::TL:
    case Handle::BR: return Qt::SizeFDiagCursor;
    case Handle::TR:
    case Handle::BL: return Qt::SizeBDiagCursor;
    case Handle::T:
    case Handle::B:  return Qt::SizeVerCursor;
    case Handle::L:
    case Handle::R:  return Qt::SizeHorCursor;
    case Handle::P0:
    case Handle::P1: return Qt::ArrowCursor; // 端点自由拖动
    case Handle::None:
    default:         return Qt::CrossCursor;
    }
}

// ---- 当前选区（全局坐标） ----

QRect SelectionController::handleRect(Handle h) const
{
    return handleRect(QRectF(m_ctx->selectionRect()), h).toRect();
}

Handle SelectionController::handleAt(const QPoint &global) const
{
    return handleAt(QRectF(m_ctx->selectionRect()), QPointF(global));
}

// ---- 框选（Selecting 态） ----

void SelectionController::beginSelect(const QPoint &global)
{
    m_origin = global;
    *m_ctx->selection = QRect(m_origin, QSize(0, 0));
    m_ctx->requestUpdate();
}

void SelectionController::dragSelect(const QPoint &global)
{
    *m_ctx->selection = QRect(m_origin, global).normalized();
    m_ctx->requestUpdate();
}

QRect SelectionController::finishSelect(const QPoint &global)
{
    return QRect(m_origin, global).normalized();
}

// ---- 编辑态：选区控制点精调 / 整体移动 ----

bool SelectionController::tryHandlePress(const QPoint &global)
{
    const Handle h = handleAt(global);
    if (h == Handle::None)
        return false;
    // 点选区控制点时取消标注选中（标注编辑与选区编辑互斥）
    if (m_ctx->selectedItem && *m_ctx->selectedItem >= 0)
    {
        *m_ctx->selectedItem = -1;
        m_ctx->selectionChanged();
    }
    m_activeHandle = h;
    m_handleStartPos = global;
    m_handleStartSel = m_ctx->selectionRect();
    return true;
}

bool SelectionController::tryStartMove(const QPoint &global)
{
    m_moving = true;
    m_moveOffset = global - m_ctx->selectionRect().topLeft();
    return true;
}

void SelectionController::dragActive(const QPoint &global)
{
    if (m_activeHandle != Handle::None)
    {
        const QRectF n = resizeRectF(QRectF(m_handleStartSel), m_activeHandle, QPointF(global), kMinSize);
        *m_ctx->selection = n.toRect().intersected(m_ctx->cap->desktop);
        m_ctx->selectionChanged(); // 调整控制点：工具栏/文字栏随选区位置实时调整
    }
    else if (m_moving)
    {
        // 整体移动：只夹紧位置到桌面内（选区大小不变）。
        // 撞到屏幕边缘时选区顶住不动，鼠标继续移动不改变选区
        const QRect desktop = m_ctx->cap->desktop;
        QRect r = m_ctx->selectionRect();
        QPoint topLeft = global - m_moveOffset;
        const int minX = desktop.left();
        const int minY = desktop.top();
        topLeft.setX(qBound(minX, topLeft.x(), qMax(minX, desktop.right() - r.width() + 1)));
        topLeft.setY(qBound(minY, topLeft.y(), qMax(minY, desktop.bottom() - r.height() + 1)));
        r.moveTopLeft(topLeft);
        *m_ctx->selection = r;
        m_ctx->selectionChanged(); // 工具栏随选区移动
    }
}

void SelectionController::cancelActive()
{
    m_activeHandle = Handle::None;
    m_moving = false;
}

// ---- 绘制 ----

void SelectionController::paintMaskAndFrame(QPainter &p, const QRect &windowRect, bool selecting,
                                            const QRect &highlightRect) const
{
    const QRect sel = m_ctx->selectionRect().translated(-m_ctx->cap->desktop.topLeft());
    if (sel.isEmpty() && highlightRect.isEmpty())
        return;

    // 灰色蒙版：选区 / 预高亮区域外变暗，区域内保持原图（挖空全透明）
    QPainterPath mask;
    mask.addRect(windowRect);
    if (!sel.isEmpty())
        mask.addRect(sel);
    if (!highlightRect.isEmpty())
        mask.addRect(highlightRect);
    p.fillPath(mask, QColor(0, 0, 0, 110));

    if (selecting && !sel.isEmpty())
    {
        // 框选过程中：浅蓝虚线框
        p.setPen(QPen(QColor(0, 150, 255), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(sel);
    }
}

void SelectionController::paintSelectionFrame(QPainter &p, const QRect &sel) const
{
    // 蓝色 1.5pt 实线边框
    p.setPen(QPen(QColor(0, 120, 255), 1.5, Qt::SolidLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(sel);

    // 8 个控制点（白色方块 + 蓝色描边）
    const Handle handles[] = {
        Handle::TL, Handle::T, Handle::TR,
        Handle::R, Handle::BR, Handle::B,
        Handle::BL, Handle::L};
    p.setBrush(Qt::white);
    p.setPen(QPen(QColor(0, 120, 255), 1));
    for (Handle h : handles)
        p.drawRect(handleRect(h).translated(-m_ctx->cap->desktop.topLeft()));

    // 尺寸提示（物理分辨率 = 最终输出尺寸）
    p.setPen(Qt::white);
    const QSize phys = ScreenCapturer::toPhysicalRect(m_ctx->selectionRect()).size();
    p.drawText(sel.translated(4, -16),
               QString("%1 × %2").arg(phys.width()).arg(phys.height()));
}

} // namespace snipaste
