#pragma once

#include "BrushTool.h"

namespace poptools {

// ============================================================
// 马赛克工具：沿笔刷足迹像素化屏幕源图像
//
// 拖动过程仅显示半透明占位笔迹（避免每帧截屏开销）；
// 释放时经 ToolContext::buildMosaic 生成马赛克标注并提交。
// ============================================================
class MosaicTool : public BrushTool
{
public:
    explicit MosaicTool(ToolContext *ctx) : BrushTool(ctx) {}

    AnnotationType annotationType() const override { return AnnotationType::Mosaic; }

    // 草稿为半透明占位（最终为像素化图像）
    QColor draftColor() const override { return QColor(0, 0, 0, 70); }

    // 马赛克笔刷宽度：与涂鸦笔一致随线宽缩放；
    // 6px 下限保证小线宽下马赛克块仍可辨（此前为线宽 5 倍、过粗）
    qreal strokeWidth() const override { return qMax(6.0, m_ctx->canvas->width); }

    void commitStroke() override
    {
        if (m_points.isEmpty() || !m_ctx->buildMosaic)
            return;
        const AnnotationItem it = m_ctx->buildMosaic(m_points, strokeWidth());
        if (it.mosaicImage.isNull())
            return; // 足迹过小无可见区域 → 不提交
        commitItem(m_ctx, it);
    }

    void paintDraft(QPainter &p) const override
    {
        if (m_points.isEmpty())
            return;
        const qreal w = strokeWidth();
        p.setPen(QPen(QColor(0, 0, 0, 70), w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(m_points);
        // 单点 / 端点处补圆点，保证笔刷为圆头
        for (const QPointF &pt : m_points)
            p.drawEllipse(pt, w / 2.0, w / 2.0);
    }
};

} // namespace poptools
