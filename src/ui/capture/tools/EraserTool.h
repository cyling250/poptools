#pragma once

#include "ToolBase.h"

namespace poptools {

// ============================================================
// 橡皮擦：按下命中删除最近标注（eraseAt 内含撤销快照），无草稿
// ============================================================
class EraserTool : public ToolBase
{
public:
    explicit EraserTool(ToolContext *ctx) : ToolBase(ctx) {}

    void onPress(const QPointF &local) override
    {
        if (m_ctx->canvas->eraseAt(local))
            m_ctx->stateChanged(); // 撤销栈变化 → 工具栏撤销按钮同步
        m_ctx->requestUpdate();
    }

    void onDrag(const QPointF &) override {}
    void onRelease(const QPointF &) override {}
    void paintDraft(QPainter &) const override {}

    Qt::CursorShape cursor() const override { return Qt::PointingHandCursor; }
};

} // namespace poptools
