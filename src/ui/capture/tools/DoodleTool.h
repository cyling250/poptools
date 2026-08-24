#pragma once

#include "BrushTool.h"

namespace snipaste {

// ============================================================
// 涂鸦笔：笔迹标注（与铅笔同为折线笔刷，类型区分便于后续差异渲染）
// ============================================================
class DoodleTool : public BrushTool
{
public:
    explicit DoodleTool(ToolContext *ctx) : BrushTool(ctx) {}

    AnnotationType annotationType() const override { return AnnotationType::Doodle; }
};

} // namespace snipaste
