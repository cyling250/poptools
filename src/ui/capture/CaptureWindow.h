#pragma once

#include <QWidget>
#include <QPixmap>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <memory>
#include <atomic>
#include <vector>

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
class OcrPanel;
class Translator;

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

    // 复制当前截图（含标注）到剪贴板并关闭截图模式（F3 贴图的截图模式预处理），
    // 返回本次选区（全局逻辑矩形）；无有效选区返回空 QRect
    QRect copyToClipboardAndClose();

signals:
    // 完成截图（选中区域画面，含标注）+ 选区全局逻辑矩形（F3 贴图定位用）；null 表示取消
    void captured(const QPixmap &pixmap, const QRect &selectionLogical);

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

    // 智能框选：鼠标下方预先高亮的窗口矩形（全局逻辑坐标，Selecting 态检测）
    QRect m_smartRect;
    QTimer m_smartDetectTimer; // 智能框选检测定时器（与鼠标事件解耦，避免检测同步阻塞拖慢取色卡片）
    QPoint m_smartMousePos;    // 当前鼠标位置缓存（定时器据此检测）
    QPoint m_smartLastPos;     // 上次检测位置（位置未变则跳过检测）
    // 智能框选候选区：图像边缘 + 系统窗口合并后的候选矩形（全局逻辑坐标），一次捕获懒加载。
    // 候选区构建在 QtConcurrent 工作线程异步执行（窗口枚举/图像识别不能在 GUI 线程同步跑，
    // EnumWindows 在 GUI 线程有死锁风险），完成后回到主线程生效。
    std::vector<QRect> m_smartCandidates;
    bool m_smartCandidatesReady = false;
    std::atomic<bool> m_smartCandidatesBusy{false}; // 候选区构建是否进行中（防重复触发）
    int m_smartGen = 0; // 捕获代次：丢弃过期（上一轮截屏）的异步结果
    void onSmartDetect();          // 定时器回调：位置变化时在候选区下重新选择最合适区域
    void rebuildSmartCandidates(); // 异步构建候选区（eSearch 窗口识别算法，工作线程执行）
    // 按下预高亮区域的待判定状态：未拖动 = 点选该区域；拖动超阈值 = 转为普通框选
    QRect m_smartPressRect;     // 按下时保存的预高亮矩形
    bool m_smartPressPending = false;
    QPoint m_smartPressPos;     // 按下点（拖动转框选时作为锚点）

    // 取色放大卡片（Selecting 态）：screen 像素图像懒加载缓存
    mutable QImage m_screenImage;

    // ---- OCR 扫描动效（点击【文】时选区内的扫描线，提示用户"没卡死"） ----
    bool m_ocrScanning = false;
    qreal m_ocrScanPos = 0.0; // 扫描线位置 0.0~1.0（选区顶部→底部）
    QTimer m_ocrScanTimer;

    // ---- 功能控制器（组合模式：构造时装配并注册） ----
    CaptureContext m_ctx;                                // 共享上下文（依赖注入）
    std::unique_ptr<SelectionController> m_selectionCtrl;
    std::unique_ptr<AnnotationEditor> m_annotationEditor;
    std::unique_ptr<ToolDrawingController> m_toolDrawing;
    std::unique_ptr<TextEditingController> m_textEditCtrl;
    std::unique_ptr<OcrController> m_ocrCtrl;
    std::unique_ptr<OutputController> m_outputCtrl;
    std::unique_ptr<OcrPanel> m_ocrPanel;                // OCR 文字栏（面板模式）
    std::unique_ptr<Translator> m_translator;            // 翻译服务（文字栏翻译）

    // ---- 帮助函数 ----
    QPointF toLocal(const QPoint &global) const; // 全局 → 选区局部
    void enterEditingState();  // 框选完成 → 编辑态（显示工具栏）
    void positionToolbar();    // 工具栏贴画布下方靠右
    void exitCapture();        // 离开截图模式（隐藏窗口 + 工具栏）
    void confirm();            // 确认截图（Enter / 双击）
    void cancel();             // 取消截图（Esc）
    void resetInteraction();   // 停止选区/标注的拖动缩放（文字编辑等入口）
    void showOcrPanel(const QString &text); // 面板模式：显示文字栏（OCR 文字）
    void positionOcrPanel();   // 文字栏定位：选区外（右侧/上方）优先，放不下则选区内
    QColor screenColorAt(const QPoint &logicalGlobal) const; // 鼠标下像素颜色（取色卡片）
    void paintColorPicker(QPainter &p) const; // 绘制取色放大卡片（Selecting 态）

    // 工具栏槽（路由到控制器）
    void onToolSelected(Tool t);
    void onColorSelected(const QColor &c);
    void onFontSizeSelected(qreal pt);
    void onOcrRequested();        // 【文】：OCR 结果以文本框覆盖到画布，不打开面板
    void onTranslateRequested();  // 【译】：OCR + 翻译，自动打开面板显示原文与翻译结果
};

} // namespace snipaste
