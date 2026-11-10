#pragma once

#include <cmath>

#include "ToolBase.h"

namespace poptools {

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
        // 最小长度门槛（R3 #10）：单击未拖动不提交——零长线段是"隐形项"，
        // 零长箭头方向未定义会画出乱箭头（误点点子请用笔刷工具）
        const QPointF d = m_end - m_start;
        if (std::hypot(d.x(), d.y()) < 2.0)
            return;
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

} // namespace poptools
