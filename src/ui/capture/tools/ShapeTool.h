#pragma once

#include "ToolBase.h"

namespace poptools {

// ============================================================
// 矩形 / 椭圆工具：按下定起点，拖动定对角，释放提交
// ============================================================
class ShapeTool : public ToolBase
{
public:
    ShapeTool(AnnotationType type, ToolContext *ctx)
        : ToolBase(ctx), m_type(type) {}

    void onPress(const QPointF &local) override
    {
        m_start = local;
        m_rect = QRectF(local, local);
        m_ctx->requestUpdate();
    }

    void onDrag(const QPointF &local) override
    {
        m_rect = QRectF(m_start, local).normalized();
        m_ctx->requestUpdate();
    }

    void onRelease(const QPointF &local) override
    {
        m_rect = QRectF(m_start, local).normalized();
        // 最小尺寸门槛（R3 #10）：单击未拖动不提交，避免 0 尺寸"隐形项"
        // 可被点选/框选却看不见（框选的 kMinSize=8 是选区提交语义，此处
        // 只需拦住误点，2px 足够；纯横/竖拖出的线形矩形仍可见，保留）
        if (m_rect.width() < 2.0 && m_rect.height() < 2.0)
            return;
        AnnotationItem it = draftItem();
        commitItem(m_ctx, it);
    }

    void paintDraft(QPainter &p) const override
    {
        AnnotationPainter::drawOne(p, draftItem());
    }

private:
    // 当前草稿 / 提交项（颜色、线宽、填充取画板当前状态）
    AnnotationItem draftItem() const
    {
        AnnotationItem it;
        it.type = m_type;
        it.color = m_ctx->canvas->color;
        it.width = m_ctx->canvas->width;
        it.filled = m_ctx->canvas->filled;
        it.rect = m_rect;
        return it;
    }

    AnnotationType m_type;
    QPointF m_start;
    QRectF m_rect;
};

} // namespace poptools
