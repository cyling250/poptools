#pragma once

#include <QPointF>
#include <QPolygonF>
#include <QPainter>
#include <memory>

#include "tools/ToolFactory.h"
#include "CaptureContext.h"

namespace snipaste {

// ============================================================
// 工具绘制控制器：按当前工具创建工具实例（策略模式 + 工厂），
// 分发按下 / 拖动 / 释放三阶段交互并渲染草稿；
// 维护工具上下文（ToolContext）并把马赛克生成、文字编辑等
// 回调桥接到 CaptureContext。
// ============================================================
class ToolDrawingController
{
public:
    explicit ToolDrawingController(CaptureContext *ctx) : m_ctx(ctx) {}

    void setTool(Tool t);             // 创建工具实例（None=选择模式，由窗口处理）
    Tool tool() const;                // 当前工具（读画板状态）
    bool isDrawing() const { return m_drawing; }
    bool hasDraft() const { return m_drawing && m_tool != nullptr; }

    void start(const QPointF &local); // 工具按下
    void drag(const QPointF &local);  // 工具拖动
    void finish(const QPointF &local);// 工具释放
    void paintDraft(QPainter &p) const;
    Qt::CursorShape cursor() const;   // 当前工具光标

private:
    AnnotationItem buildMosaicItem(const QPolygonF &points, qreal width) const; // 按笔迹生成马赛克标注

    CaptureContext *m_ctx;
    ToolContext m_toolCtx;
    std::unique_ptr<ToolBase> m_tool;
    bool m_drawing = false;           // 工具按下至释放期间
};

} // namespace snipaste
