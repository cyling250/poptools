#pragma once

#include "ToolBase.h"

namespace snipaste {

// ============================================================
// 笔刷工具基类：按笔迹（折线）累积顶点，拖动实时描画，释放提交
//
// 子类只需提供标注类型 / 草稿色 / 笔刷宽度；马赛克等
// 特殊提交（生成马赛克图）可重写 commitStroke()。
// ============================================================
class BrushTool : public ToolBase
{
public:
    explicit BrushTool(ToolContext *ctx) : ToolBase(ctx) {}

    void onPress(const QPointF &local) override
    {
        m_points.clear();
        m_points << local;
        m_ctx->requestUpdate();
    }

    void onDrag(const QPointF &local) override
    {
        m_points << local;
        m_ctx->requestUpdate();
    }

    void onRelease(const QPointF &local) override
    {
        if (m_points.isEmpty())
            return;
        // 释放点已由最后一次 onDrag 添加（无拖动时由 onPress 添加），不再重复
        commitStroke();
    }

    void paintDraft(QPainter &p) const override
    {
        if (m_points.isEmpty())
            return;
        p.setPen(QPen(draftColor(), strokeWidth(), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(m_points);
    }

    virtual AnnotationType annotationType() const = 0; // 提交的标注类型
    virtual QColor draftColor() const { return m_ctx->canvas->color; } // 草稿色（默认画板色）
    virtual qreal strokeWidth() const { return m_ctx->canvas->width; } // 笔刷宽度（默认线宽）

    // 释放时提交标注（马赛克等子类重写）
    virtual void commitStroke()
    {
        AnnotationItem it;
        it.type = annotationType();
        it.color = draftColor();
        it.width = strokeWidth();
        it.points = m_points;
        commitItem(m_ctx, it);
    }

protected:
    QPolygonF m_points;
};

} // namespace snipaste
