#pragma once

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <QImage>
#include <QTimer>
#include <memory>

#include "core/annotation/AnnotationCanvas.h"
#include "core/annotation/AnnotationItem.h"
#include "core/annotation/AnnotationPainter.h"
#include "core/capture/ScreenCapturer.h"
#include "core/ocr/OcrService.h"
#include "tools/ToolFactory.h"

class QScreen;  // Qt 全局类
class QTextEdit;
class QTextCharFormat;

namespace snipaste {

class ToolBar; // 前向声明（ToolBar 定义于本命名空间）

// ============================================================
// 截图画布窗口（全屏覆盖）
//
// 交互流程（状态机）：
//   Idle      → 按下 F1 后 startCapture()
//   Selecting → 拖拽框选，实时显示选区
//   Editing   → 松开鼠标进入编辑态：
//                 * 选区高亮（外部灰色蒙版），蓝色 1.5pt 边框
//                 * 8 个控制点（四角+四边中点）可拖动精调选区
//                 * 底部弹出标注工具栏（矩形/椭圆/箭头/折线/
//                   画笔/橡皮/文本/取色 + 颜色 + 撤销/重做/保存/复制/完成）
//                 * Enter / 双击选区 确认截图
//                 * 选择模式：ESC 连按两次退出（第一次显示 2 秒提示）；工具模式：ESC 返回选择模式
//
// 标注：AnnotationCanvas 管理标注项（选区局部坐标），
//       confirm() 时将标注合成到输出 pixmap。
// 屏幕捕获与 DPI 坐标映射由 ScreenCapturer 提供。
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

    // 编辑控制点（定义于 core 层 AnnotationPainter.h：TL..L 角/边中点 + P0/P1 线段端点）
    using Handle = ::snipaste::Handle;

    QRect handleRect(Handle h) const;          // 控制点方块区域（全局坐标）
    QRectF handleRect(const QRectF &r, Handle h) const; // 控制点方块（r 所在坐标系，选区用）
    QRectF handleRect(const AnnotationItem &it, Handle h) const; // 控制点方块（标注局部坐标，含线段端点）
    Handle handleAt(const QPoint &pos) const;  // 命中检测（全局坐标，选区控制点）
    Handle handleAt(const QRectF &r, const QPointF &pos) const; // 命中检测（r 所在坐标系）
    Handle handleAt(const AnnotationItem &it, const QPointF &pos) const; // 命中检测（标注控制点）
    QRectF resizeRectF(QRectF r, Handle h, const QPointF &pos, qreal minSize) const; // 按控制点调矩形（纯函数，选区用）
    void resizeSelection(Handle h, const QPoint &pos); // 按控制点调整选区
    void setCursorFor(Handle h);

    void enterEditingState();  // 框选完成 → 编辑态（显示工具栏）
    void positionToolbar();    // 工具栏贴画布下方靠右
    void exitCapture();        // 离开截图模式（隐藏窗口 + 工具栏）

    // ---- 标注工具事件处理（选区内局部坐标） ----
    QPointF toLocal(const QPoint &global) const;      // 全局 → 选区局部
    void startDraw(const QPointF &pos);               // 工具按下
    void continueDraw(const QPointF &pos);            // 工具拖动
    void finishDraw(const QPointF &pos);              // 工具释放
    AnnotationItem buildMosaicItem(const QPolygonF &points, qreal width) const; // 按笔迹生成马赛克标注

    // ---- 选择模式：标注项选中 / 编辑 ----
    void moveSelectedItem(const QPointF &local);      // 拖动选中项（快照 + 实时替换）
    void resizeSelectedItem(const QPointF &local);    // 拖动选中项控制点缩放

    // ---- 文字内嵌输入框（PPT 风格） ----
    void beginTextEdit(const QPointF &local, int itemIndex); // itemIndex=-1 新建，否则重编辑
    void commitTextEdit();                                  // 空输入删除 / 有输入保存
    void adjustTextEditSize();                              // 输入框尺寸自适应（宽=最长行，高=总行高）
    void updateFontSizeDisplay();                           // 字号栏反映当前选中/编辑文本的大小
    void onFontSizeSelected(qreal pt);                      // 字号提交：立即应用到选中文本 / 设为后续输入字号
    void applyFontSizeToItem(AnnotationItem &it, qreal pt); // 整条文字标注改为统一字号（富文本 + 重算框尺寸）
    qreal fragmentPt(const QTextCharFormat &fmt, const QFont &def) const; // 片段字号（pt，继承默认时回退）

    void confirm();  // 确认截图（Enter / 双击 / 工具栏完成）
    void cancel();   // 取消截图（Esc）

    // 工具栏槽
    void onToolSelected(Tool t);
    void onColorSelected(const QColor &c);
    void onSave();
    void onCopy();

    // ---- 画板内文字识别（OCR）：识别选区文字，以文本框覆盖到画布 ----
    void onOcrRequested();
    void onOcrBoxes(bool ok, const QVector<OcrBox> &boxes, const QString &error);

    // 输出合成：按物理分辨率裁剪选区并等比放大标注（保存/复制/确认共用）
    QPixmap renderResult() const;

    // 本次截屏数据：画面（物理像素）+ 逻辑/物理虚拟桌面几何
    CaptureResult m_cap;
    State m_state = State::Idle;
    QRect m_selection;         // 当前选区（全局坐标）

    // 框选
    QPoint m_origin;           // 框选起点
    // 控制点精调
    Handle m_activeHandle = Handle::None;
    QPoint m_handleStartPos;   // 按下控制点时的鼠标位置
    QRect m_handleStartSel;    // 按下控制点时的选区
    // 整体移动
    bool m_moving = false;
    QPoint m_moveOffset;       // 鼠标相对选区左上角的偏移

    // 选择模式 ESC 二次退出（防误触）：第一次 ESC 显示 2 秒提示，二次内连按才退出
    bool m_escArmed = false;
    QTimer m_escTimer;

    // 画板与工具栏
    AnnotationCanvas m_canvas; // 标注画板（标注项 + 撤销/重做）
    ToolBar *m_toolbar = nullptr;

    // 画板内 OCR：识别选区图像（物理分辨率），结果映射回选区局部坐标
    OcrService m_ocr;
    QImage m_ocrImg;           // 传给 OCR 的选区裁剪图（物理像素）

    // 工具体系（策略模式 + 工厂）：m_tool 为当前激活工具，m_ctx 为工具上下文
    ToolContext m_ctx;
    std::unique_ptr<ToolBase> m_tool;
    bool m_drawing = false;    // 是否正在绘制（工具按下至释放期间）

    // 文字内嵌输入框（多行）：透明底 + 虚线边框，尺寸自适应文字内容；
    // Enter/Shift+Enter 均换行，输入结束方式为鼠标点击框外区域
    QTextEdit *m_textEdit = nullptr;
    bool m_textEditing = false;   // 是否正在输入文字
    int m_textEditItem = -1;      // 正在编辑的标注索引（-1 = 新建）
    QPointF m_textEditPos;        // 新建文本的选区局部坐标
    QColor m_textEditStartColor;  // 本次输入开始时的标注色（输入期间改色不影响已输入文字：无显式色片段用此兜底）
    qreal m_defaultFontSize = 14.0; // 无文本选中时字号栏的值 = 接下来输入文字的字号（pt）

    // 选择模式：标注项选中 / 编辑
    int m_selectedItem = -1;            // 当前选中的标注索引（-1 无）
    bool m_itemMoving = false;          // 正在拖动选中项
    Handle m_itemActiveHandle = Handle::None; // 正在缩放选中项的控制点
    bool m_itemSnapshotted = false;     // 本次交互是否已压撤销快照（首笔变化时压一次）
    QPointF m_itemStartPos;             // 按下时的鼠标（选区局部坐标）
    QRectF m_itemStartBounds;           // 按下时的标注外接矩形（局部坐标）
    AnnotationItem m_itemStartGeom;     // 按下时的标注几何快照

    static constexpr int kMinSize = 8;   // 选区最小尺寸（像素）
    static constexpr int kHandle = 5;    // 控制点半边长（像素，共 10px 方块）
};

} // namespace snipaste
