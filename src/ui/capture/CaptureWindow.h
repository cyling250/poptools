#pragma once

#include <QWidget>
#include <QPixmap>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <memory>

#include "core/annotation/AnnotationCanvas.h"
#include "core/capture/ScreenCapturer.h"
#include "CaptureContext.h"

class QScreen;  // Qt 全局类
class QLabel;   // Qt 全局类

namespace poptools {

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
class SmartRectDetector;
class RapidOcrService;

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
    // ocrService：共享 OCR 引擎（应用级唯一，子进程昂贵），必须注入；
    // 为空时 OCR 不可用并告警（调试期 Q_ASSERT 兜底——原先的"未注入则自建
    // 兜底实例"已删除，见构造函数内注释）
    explicit CaptureWindow(RapidOcrService *ocrService = nullptr,
                           QWidget *parent = nullptr);

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
    // 窗口尺寸变化 → 工具栏跟随重定位；截图窗几何由 startCapture 一次设定，
    // 常规截图流程不触发（声明与定义处注释同此义）
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

    // 智能框选：鼠标下方预先高亮的"元素级"矩形（全局逻辑坐标，Selecting 态检测）。
    // 检测在 SmartRectDetector 的常驻工作线程异步执行（窗口/UIA 枚举不能在 GUI
    // 线程同步跑），结果经 found 信号回主线程；只保留最新查询（latest-wins）
    QRect m_smartRect;
    QTimer m_smartDetectTimer; // 智能框选检测定时器（与鼠标事件解耦，避免检测同步阻塞拖慢取色卡片）
    QPoint m_smartMousePos;    // 当前鼠标位置缓存（定时器据此检测）
    QPoint m_smartLastPos;     // 上次提交位置（位置未变则跳过提交，静止零开销）
    SmartRectDetector *m_smartDetector = nullptr;
    int m_smartGen = 0; // 捕获代次：丢弃过期（上一轮截屏）的异步结果
    void onSmartDetect();                        // 定时器回调：鼠标移出当前预选矩形时提交新查询
    void onSmartFound(quint64 gen, const QRect &rect); // 检测结果回包：代次/位置校验后生效
    // 按下预高亮区域的待判定状态：未拖动 = 点选该区域；拖动超阈值 = 转为普通框选
    QRect m_smartPressRect;     // 按下时保存的预高亮矩形
    bool m_smartPressPending = false;
    QPoint m_smartPressPos;     // 按下点（拖动转框选时作为锚点）
    bool m_dblReselect = false; // 双击选区外重画：待吞掉的双击尾随 release（否则 1px 退化矩形触发秒退）
    // T-062 修复（R1 疑似BUG 6）：双击重画的重选路径标志。beginSelect 会立即把
    // m_selection 覆写为新框选矩形，重选失败（拖出过小选区释放）时无法从
    // m_selection 恢复原选区——双击进入重选时把旧选区暂存 m_reselectSaved，
    // 释放"过小视为取消"判定改为恢复原选区编辑态，避免已有标注随整场取消被丢弃
    bool m_reselecting = false;  // 双击重画产生的重选进行中（首个框选路径恒为 false）
    QRect m_reselectSaved;       // 重选前的原选区（重选失败恢复用；成功/复位后失效）

    // 取色放大卡片（Selecting 态）：screen 像素图像懒加载缓存
    mutable QImage m_screenImage;
    // 底图缓存（R6-B1）：物理原图挂当前屏幕 DPR 元数据后 1:1 直绘，零重采样。
    // 旧实现缩到逻辑尺寸缓存再被设备像素比放大回去（双重重采样，糊屏根因）
    QPixmap m_basePixmap;
    qreal m_baseDpr = 0.0;      // 缓存创建时的窗口 DPR（跨屏移动会变，变则重建）
    QSize m_baseLogicalSize;    // 缓存创建时的窗口逻辑尺寸（尺寸变更则重建）
    QRect m_lastColorCardRect; // 取色卡片上次绘制矩形（悬停局部重绘用，全局逻辑→窗口坐标）

    // ---- OCR 扫描动效（点击【文】时选区内的扫描线，提示用户"没卡死"） ----
    bool m_ocrScanning = false;
    qreal m_ocrScanPos = 0.0; // 扫描线位置 0.0~1.0（选区顶部→底部）
    QTimer m_ocrScanTimer;

    // ---- ESC 退出确认（防误触）：画布有墨迹时，首次 ESC 武装并轻提示，
    //      1 秒内再按 ESC 才真正退出；按其他键 / 点击鼠标即解除并继续编辑 ----
    bool m_exitArmActive = false;
    QTimer m_exitArmTimer;        // 武装超时（自动解除，提示不常驻）
    QLabel *m_escToast = nullptr; // 「再按一次 ESC 退出」胶囊提示（不拦鼠标）
    bool hasInk() const { return !m_canvas.items.isEmpty(); } // 画布是否有墨迹（标注 / OCR 文字框）

    // ---- 功能控制器（组合模式：构造时装配并注册） ----
    CaptureContext m_ctx;                                // 共享上下文（依赖注入）
    RapidOcrService *m_ocrService = nullptr;            // 共享 OCR 引擎（非拥有，必须注入）
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
    void resetSelectionSession(); // 选区会话状态复位（startCapture / exitCapture 共用的公共子集）
    void confirm();            // 确认截图（Enter / 双击）
    void cancel();             // 取消截图（Esc）
    void armExitConfirm();    // ESC 退出确认：进入武装态（显示轻提示 + 启动超时）
    void disarmExitConfirm(); // 解除武装（隐藏轻提示、停表）；超时/其他操作也走此路
    // 武装退出判定（ESC / 右键选区外共用）：已武装 → 真正退出；未武装 → 进入武装态。
    // wasArmed 必须传"本事件入口处"的武装态——mousePressEvent 顶部会先解除武装，
    // 退出判定若读 m_exitArmActive 当前值则恒 false，"再按一次才退出"永不成立（T-062 修复）
    void handleExitIntent(bool wasArmed);
    void positionEscToast();  // 轻提示定位：工具栏上缘居中（贴近窗口上缘转下方）
    void resetInteraction();   // 停止选区/标注的拖动缩放（文字编辑等入口）
    void showOcrPanel(const QString &text); // 面板模式：显示文字栏（OCR 文字）
    void positionOcrPanel();   // 文字栏定位：选区外（右侧/上方）优先，放不下则选区内
    QColor screenColorAt(const QPoint &logicalGlobal) const; // 鼠标下像素颜色（取色卡片）
    void paintColorPicker(QPainter &p) const; // 绘制取色放大卡片（Selecting 态）
    QRect colorPickerCardRect(const QPoint &cursorPos) const; // 取色卡片几何（绘制与局部重绘共用）

    // 工具栏槽（路由到控制器）
    void onToolSelected(Tool t);
    void onColorSelected(const QColor &c);
    void onFontSizeSelected(qreal pt);
    void onOcrRequested();        // 【文】：OCR 结果以文本框覆盖到画布，不打开面板
    void onTranslateRequested();  // 【译】：OCR + 翻译，自动打开面板显示原文与翻译结果
    void onQrRequested();         // 【码】：识别选区内二维码，内容弹窗展示并复制
};

} // namespace poptools
