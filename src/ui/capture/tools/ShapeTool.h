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
