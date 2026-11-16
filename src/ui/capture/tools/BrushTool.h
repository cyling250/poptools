#pragma once

#include "ToolBase.h"

namespace poptools {

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
        Q_UNUSED(local)
        // T-062 修复（R1 疑似BUG 4，依据 R3 #10 先例"单击未拖动不提交"）。
        // 对铅笔/涂鸦是缺陷修复：单击不拖动时 m_points 只有 1 个点，drawPolyline
        // 对单点不画任何像素，原先无条件提交会产出一个看不见却能被 itemAt 选中/
        // 拖动/占撤销栈的"隐形标注"（ShapeTool/SegmentTool 已有最小尺寸门槛，
        // 笔刷族此前漏配）。
        // 对马赛克子类是【有意行为变更】（R2-F3 订正：基线单击提交的是直径≈线宽
        // 的可见马赛克圆点——mask 除单点不画的折线外还有全点圆头，并非"隐形项"）：
        // 仍按"单击不提交"停用，理由——① 与矩形/直线/铅笔/涂鸦的单击语义拉齐；
        // ② 单击更可能是误触，落点即占撤销栈且需手动清理，拖出微小笔迹可达成
        // 同样的"烙点"效果；③ ≥6px 的单点圆作为打码能力过小，无实用价值
        if (m_points.size() < 2)
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
        capEndpoints(p); // 子类补充笔刷端点圆头（默认无）
    }

    virtual AnnotationType annotationType() const = 0; // 提交的标注类型
    virtual QColor draftColor() const { return m_ctx->canvas->color; } // 草稿色（默认画板色）
    virtual qreal strokeWidth() const { return m_ctx->canvas->width; } // 笔刷宽度（默认线宽）

    // 草稿渲染扩展点：折线绘制后补充端点圆头（调用时 pen 已设好、brush=NoBrush）
    virtual void capEndpoints(QPainter &) const {}

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

} // namespace poptools
