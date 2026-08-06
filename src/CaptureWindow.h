#pragma once

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <QImage>

#include "AnnotationCanvas.h"

class ToolBar;

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
    Handle handleAt(const QPoint &pos) const;  // 命中检测
    void resizeSelection(Handle h, const QPoint &pos); // 按控制点调整选区
    void setCursorFor(Handle h);

    void enterEditingState();  // 框选完成 → 编辑态（显示工具栏）
    void exitCapture();        // 离开截图模式（隐藏窗口 + 工具栏）

    // ---- 标注工具事件处理（选区内局部坐标） ----
    QPointF toLocal(const QPoint &global) const;      // 全局 → 选区局部
    void startDraw(const QPointF &pos);               // 工具按下
    void continueDraw(const QPointF &pos);            // 工具拖动
    void finishDraw(const QPointF &pos);              // 工具释放
    void pickColor(const QPointF &pos);               // 取色器

    void confirm();  // 确认截图（Enter / 双击 / 工具栏完成）
    void cancel();   // 取消截图（Esc）

    // 工具栏槽
    void onToolSelected(AnnotationCanvas::Tool t);
    void onColorSelected(const QColor &c);
    void onSave();
    void onCopy();

    QPixmap m_screen;          // 虚拟桌面截图
    QRect m_desktop;           // 虚拟桌面几何（全局坐标）
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
    bool m_drawing = false;    // 是否正在绘制
    QVector<QPointF> m_polyPts; // 折线绘制中的临时顶点
    QColor m_pickedColor;      // 取色器取到的颜色

    static constexpr int kMinSize = 8;   // 选区最小尺寸（像素）
    static constexpr int kHandle = 5;    // 控制点半边长（像素，共 10px 方块）
};
