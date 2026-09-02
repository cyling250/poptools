#pragma once

#include <memory>

#include "BrushTool.h"
#include "DoodleTool.h"
#include "EraserTool.h"
#include "MosaicTool.h"
#include "PencilTool.h"
#include "SegmentTool.h"
#include "ShapeTool.h"
#include "TextTool.h"

namespace poptools {

// ============================================================
// 工具工厂：按 Tool 枚举创建对应的工具实例（策略模式）
// None = 选择模式 → 返回 nullptr（由窗口处理）
// ============================================================
class ToolFactory
{
public:
    static std::unique_ptr<ToolBase> create(Tool t, ToolContext *ctx)
    {
        switch (t) {
        case Tool::Rect:    return std::make_unique<ShapeTool>(AnnotationType::Rect, ctx);
        case Tool::Ellipse: return std::make_unique<ShapeTool>(AnnotationType::Ellipse, ctx);
        case Tool::Line:    return std::make_unique<SegmentTool>(AnnotationType::Line, ctx);
        case Tool::Arrow:   return std::make_unique<SegmentTool>(AnnotationType::Arrow, ctx);
        case Tool::Pencil:  return std::make_unique<PencilTool>(ctx);
        case Tool::Doodle:  return std::make_unique<DoodleTool>(ctx);
        case Tool::Mosaic:  return std::make_unique<MosaicTool>(ctx);
        case Tool::Text:    return std::make_unique<TextTool>(ctx);
        case Tool::Eraser:  return std::make_unique<EraserTool>(ctx);
        case Tool::None:
        default:            return nullptr;
        }
    }
};

} // namespace poptools
