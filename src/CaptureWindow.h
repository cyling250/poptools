#pragma once

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <QImage>

#include "AnnotationCanvas.h"

class ToolBar;
class QScreen;
class QLineEdit;

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
//                 * Esc 取消退出
//
// 标注：AnnotationCanvas 管理标注项（选区局部坐标），
//       confirm() 时将标注合成到输出 pixmap
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

    // 8 个控制点：四角 + 四边中点
    enum class Handle {
        None,
        TL, T, TR,   // 上左 / 上中 / 上右
        R, BR, B,    // 右中 / 右下 / 下中
        BL, L        // 左下 / 左中
    };

    QRect handleRect(Handle h) const;          // 控制点方块区域（全局坐标）
    QRectF handleRect(const QRectF &r, Handle h) const; // 控制点方块（r 所在坐标系，选中标注用）
    Handle handleAt(const QPoint &pos) const;  // 命中检测（全局坐标）
    Handle handleAt(const QRectF &r, const QPointF &pos) const; // 命中检测（r 所在坐标系）
    QRectF resizeRectF(QRectF r, Handle h, const QPointF &pos, qreal minSize) const; // 按控制点调矩形（纯函数）
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
    void buildMosaicImage();                          // 马赛克像素化

    // ---- 选择模式：标注项选中 / 编辑 ----
    void moveSelectedItem(const QPointF &local);      // 拖动选中项（快照 + 实时替换）
    void resizeSelectedItem(const QPointF &local);    // 拖动选中项控制点缩放

    // ---- 文字内嵌输入框（PPT 风格） ----
    void beginTextEdit(const QPointF &local, int itemIndex); // itemIndex=-1 新建，否则重编辑
    void commitTextEdit();                                  // 空输入删除 / 有输入保存

    void confirm();  // 确认截图（Enter / 双击 / 工具栏完成）
    void cancel();   // 取消截图（Esc）

    // 工具栏槽
    void onToolSelected(AnnotationCanvas::Tool t);
    void onColorSelected(const QColor &c);
    void onSave();
    void onCopy();

    // ---- DPI：逻辑坐标（窗口/鼠标/选区）↔ 物理坐标（m_screen） ----
    QPoint toPhysical(const QPoint &logicalGlobal) const;  // 逻辑全局点 → 物理全局点
    QRect selectionToPhysical() const;                     // 选区（逻辑全局）→ 物理像素矩形

    QPixmap m_screen;          // 虚拟桌面截图（物理分辨率，dpr=1）
    QRect m_desktop;           // 虚拟桌面几何（逻辑坐标，窗口/鼠标/选区均用）
    QRect m_physDesktop;       // 虚拟桌面几何（物理像素，m_screen 坐标系）
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

    // 画板与工具栏
    AnnotationCanvas m_canvas; // 标注画板（标注项 + 撤销/重做）
    ToolBar *m_toolbar = nullptr;
    AnnotationItem m_draft;    // 正在绘制的标注（局部坐标）
    QPointF m_draftOrigin;     // 绘制按下点（矩形/椭圆拖动锚点，避免随鼠标漂移）
    bool m_drawing = false;    // 是否正在绘制

    // 文字内嵌输入框（PPT 风格：无输入点击别处删除 / 有输入点击别处保存）
    QLineEdit *m_textEdit = nullptr;
    bool m_textEditing = false;   // 是否正在输入文字
    int m_textEditItem = -1;      // 正在编辑的标注索引（-1 = 新建）
    QPointF m_textEditPos;        // 新建文本的选区局部坐标

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
