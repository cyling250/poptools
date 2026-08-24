#pragma once

#include "ToolBase.h"

namespace snipaste {

// ============================================================
// 直线 / 箭头工具：按下定起点，拖动定终点，释放提交
// ============================================================
class SegmentTool : public ToolBase
{
public:
    SegmentTool(AnnotationType type, ToolContext *ctx)
        : ToolBase(ctx), m_type(type) {}

    void onPress(const QPointF &local) override
    {
        m_start = local;
        m_end = local;
        m_ctx->requestUpdate();
    }

    void onDrag(const QPointF &local) override
    {
        m_end = local;
        m_ctx->requestUpdate();
    }

    void onRelease(const QPointF &local) override
    {
        m_end = local;
        commitItem(m_ctx, draftItem());
    }

    void paintDraft(QPainter &p) const override
    {
        AnnotationPainter::drawOne(p, draftItem());
    }

private:
    AnnotationItem draftItem() const
    {
        AnnotationItem it;
        it.type = m_type;
        it.color = m_ctx->canvas->color;
        it.width = m_ctx->canvas->width;
        it.points = {m_start, m_end};
        return it;
    }

    AnnotationType m_type;
    QPointF m_start;
    QPointF m_end;
};

} // namespace snipaste
