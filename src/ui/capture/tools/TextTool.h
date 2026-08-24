#pragma once

#include "ToolBase.h"

namespace snipaste {

// ============================================================
// 文字工具：按下启动文字内嵌编辑（新建），无草稿交互
// （输入交互全部由 TextEditingController 承接）
// ============================================================
class TextTool : public ToolBase
{
public:
    explicit TextTool(ToolContext *ctx) : ToolBase(ctx) {}

    void onPress(const QPointF &local) override
    {
        if (m_ctx->beginText)
            m_ctx->beginText(local, -1); // -1 新建文字标注
    }

    void onDrag(const QPointF &) override {}
    void onRelease(const QPointF &) override {}
    void paintDraft(QPainter &) const override {}
};

} // namespace snipaste
