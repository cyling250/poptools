#pragma once

#include "BrushTool.h"

namespace poptools {

// ============================================================
// 铅笔（自由画笔）：单色细笔迹
// ============================================================
class PencilTool : public BrushTool
{
public:
    explicit PencilTool(ToolContext *ctx) : BrushTool(ctx) {}

    AnnotationType annotationType() const override { return AnnotationType::Freehand; }
};

} // namespace poptools
