#pragma once

#include <QPointF>
#include <QRect>
#include <QColor>
#include <functional>

#include "core/annotation/AnnotationCanvas.h"
#include "core/capture/ScreenCapturer.h"

class QPainter;

namespace snipaste {

class ToolBar;

// 控制点半边长（像素，共 10px 方块）与选区最小尺寸
constexpr int kHandle = 5;
constexpr int kMinSize = 8;

// ============================================================
// 截图窗口共享上下文（依赖注入）
//
// 截图的各功能控制器（选区 / 标注项编辑 / 工具绘制 / 文字编辑 /
// OCR / 输出）只依赖本结构提供的共享状态与窗口回调，与
// CaptureWindow 具体实现解耦；由 CaptureWindow 装配本结构并
// 注册到各控制器（组合 + 门面模式）。
// ============================================================
struct CaptureContext
{
    CaptureResult *cap = nullptr;       // 屏幕画面 + 物理/逻辑虚拟桌面几何
    AnnotationCanvas *canvas = nullptr; // 标注画板
    ToolBar *toolbar = nullptr;         // 标注工具栏
    QRect *selection = nullptr;         // 当前选区（全局逻辑坐标）
    int *selectedItem = nullptr;        // 当前选中的标注索引（-1 无）

    // 窗口回调
    std::function<void()> requestUpdate;    // 请求重绘画布
    std::function<void()> stateChanged;     // 画布/撤销栈变化 → 工具栏撤销/重做同步 + 重绘
    std::function<void()> selectionChanged; // 选区/选中项变化 → 工具栏定位 + 字号栏刷新
    std::function<void()> exitRequested;    // 请求退出截图界面（保存/复制完成）
    std::function<void(const QPointF &, int)> beginTextEdit; // 启动文字编辑（-1 新建，否则重编辑）

    // 当前工具草稿（输出合成时叠加；由 ToolDrawingController 提供）
    std::function<bool()> hasDraft;
    std::function<void(QPainter &)> paintDraft;

    // 面板模式：OCR 识别文字显示到文字栏（由 CaptureWindow 提供 OcrPanel）
    std::function<void(const QString &)> showOcrPanel;

    QRect selectionRect() const { return selection ? *selection : QRect(); }
    int selectedIndex() const { return selectedItem ? *selectedItem : -1; }
};

} // namespace snipaste
