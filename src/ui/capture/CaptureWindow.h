#pragma once

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <memory>

#include "core/annotation/AnnotationCanvas.h"
#include "core/capture/ScreenCapturer.h"
#include "CaptureContext.h"

class QScreen;  // Qt 全局类

namespace snipaste {

class ToolBar;
class CaptureContext;
class SelectionController;
class AnnotationEditor;
class ToolDrawingController;
class TextEditingController;
class OcrController;
class OutputController;

// ============================================================
// 截图画布窗口（全屏覆盖）—— 门面 + 事件路由
//
// 本类只保留：对外接口（startCapture / captured）、窗口状态机
// （Idle → Selecting → Editing）、鼠标/键盘事件分发、绘制编排，
// 以及功能控制器的装配（组合模式）。
//
// 各功能在独立文件中实现并注册到本窗口：
//   - 选区框选/移动/控制点   SelectionController
//   - 标注项选中/拖动/缩放   AnnotationEditor
//   - 工具绘制（矩形/线条…） ToolDrawingController
//   - 文字内嵌编辑            TextEditingController
//   - 选区 OCR                OcrController
//   - 输出合成/保存/复制      OutputController
// 控制器经 CaptureContext（共享状态 + 窗口回调）与窗口解耦。
// ============================================================
class CaptureWindow : public QWidget
{
    Q_OBJECT
public:
    explicit CaptureWindow(QWidget *parent = nullptr);

    // 启动截图（捕获虚拟桌面画面并全屏显示）
    void startCapture();

signals:
    // 完成截图（选中区域画面，含标注）；null 表示取消
    void captured(const QPixmap &pixmap);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class State { Idle, Selecting, Editing };

    // ---- 窗口状态 ----
    CaptureResult m_cap;                 // 屏幕画面 + 物理/逻辑虚拟桌面几何
    State m_state = State::Idle;
    QRect m_selection;                   // 当前选区（全局逻辑坐标）
    AnnotationCanvas m_canvas;           // 标注画板（标注 + 撤销/重做）
    ToolBar *m_toolbar = nullptr;
    int m_selectedItem = -1;             // 当前选中的标注索引

    // 选择模式 ESC 二次退出（防误触）：第一次 ESC 显示 2 秒提示，二次内连按才退出
    bool m_escArmed = false;
    QTimer m_escTimer;

    // ---- 功能控制器（组合模式：构造时装配并注册） ----
    CaptureContext m_ctx;                                // 共享上下文（依赖注入）
    std::unique_ptr<SelectionController> m_selectionCtrl;
    std::unique_ptr<AnnotationEditor> m_annotationEditor;
    std::unique_ptr<ToolDrawingController> m_toolDrawing;
    std::unique_ptr<TextEditingController> m_textEditCtrl;
    std::unique_ptr<OcrController> m_ocrCtrl;
    std::unique_ptr<OutputController> m_outputCtrl;

    // ---- 帮助函数 ----
    QPointF toLocal(const QPoint &global) const; // 全局 → 选区局部
    void enterEditingState();  // 框选完成 → 编辑态（显示工具栏）
    void positionToolbar();    // 工具栏贴画布下方靠右
    void exitCapture();        // 离开截图模式（隐藏窗口 + 工具栏）
    void confirm();            // 确认截图（Enter / 双击）
    void cancel();             // 取消截图（Esc）
    void resetInteraction();   // 停止选区/标注的拖动缩放（文字编辑等入口）

    // 工具栏槽（路由到控制器）
    void onToolSelected(Tool t);
    void onColorSelected(const QColor &c);
    void onFontSizeSelected(qreal pt);
    void onOcrRequested();
};

} // namespace snipaste
