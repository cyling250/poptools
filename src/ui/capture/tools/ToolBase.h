#pragma once

#include <QPainter>
#include <functional>

#include "core/annotation/AnnotationCanvas.h"
#include "core/annotation/AnnotationPainter.h"
#include "core/capture/ScreenCapturer.h"

namespace snipaste {

class ToolBar;

// ============================================================
// 工具上下文（策略模式注入）：工具绘制时所需的画板 / 屏幕 /
// 工具栏引用与窗口回调，由 ToolDrawingController::setTool 组装
// ============================================================
struct ToolContext
{
    AnnotationCanvas *canvas = nullptr;   // 标注画板（颜色 / 线宽 / 填充 / 工具状态）
    CaptureResult *cap = nullptr;         // 屏幕画面 + 物理/逻辑几何
    ToolBar *toolbar = nullptr;           // 标注工具栏
    QRect selection;                      // 工具激活时的选区快照（全局逻辑坐标）

    std::function<void()> requestUpdate;  // 请求重绘画布
    std::function<void()> stateChanged;   // 画布/撤销栈变化 → 工具栏同步 + 重绘
    std::function<AnnotationItem(const QPolygonF &points, qreal width)> buildMosaic; // 按笔迹生成马赛克标注
    std::function<void(const QPointF &local, int existing)> beginText;               // 启动文字编辑（-1 新建）
};

// ============================================================
// 标注工具基类：按下 / 拖动 / 释放三阶段 + 草稿渲染 + 光标
// ============================================================
class ToolBase
{
public:
    explicit ToolBase(ToolContext *ctx) : m_ctx(ctx) {}
    virtual ~ToolBase() = default;

    virtual void onPress(const QPointF &local) = 0;   // 工具按下
    virtual void onDrag(const QPointF &local) = 0;    // 工具拖动
    virtual void onRelease(const QPointF &local) = 0; // 工具释放（提交标注）
    virtual void paintDraft(QPainter &p) const = 0;   // 绘制交互草稿（painter 在选区局部坐标）
    virtual Qt::CursorShape cursor() const { return Qt::CrossCursor; }

protected:
    ToolContext *m_ctx;
};

// 提交标注到画板（压撤销栈）并通知状态变化 / 重绘
inline void commitItem(ToolContext *ctx, const AnnotationItem &it)
{
    ctx->canvas->addItem(it);
    ctx->stateChanged();
    ctx->requestUpdate();
}

} // namespace snipaste
