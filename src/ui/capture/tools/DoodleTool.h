#pragma once

#include "BrushTool.h"

namespace poptools {

// ============================================================
// 涂鸦笔：半透明"划重点"式笔迹笔刷
// ============================================================
class DoodleTool : public BrushTool
{
public:
    explicit DoodleTool(ToolContext *ctx) : BrushTool(ctx) {}

    AnnotationType annotationType() const override { return AnnotationType::Doodle; }

    // 划重点式半透明：把 alpha 烘焙进标注色。渲染端直接以 it.color 描线，
    // 故拖动草稿预览与最终渲染（画布/复制/保存）天然一致；
    // drawPolyline 为单次描边，同一笔迹内自交不会叠加变深，
    // 跨笔画重叠处像真实荧光笔一样加深
    QColor draftColor() const override
    {
        QColor c = m_ctx->canvas->color;
        c.setAlpha(115); // 约 45% 不透明度
        return c;
    }
};

} // namespace poptools
