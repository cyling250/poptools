#pragma once

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QRect>

// ============================================================
// 截图画布窗口（全屏覆盖）
//
// 交互流程（状态机）：
//   Idle      → 按下 F1 后 startCapture()
//   Selecting → 拖拽框选，实时显示选区
//   Editing   → 松开鼠标进入编辑态：
//                 * 选区高亮（外部灰色蒙版），蓝色 1.5pt 边框
//                 * 8 个控制点（四角+四边中点）可拖动精调选区
//                 * 点击选区内可整体移动选区
//                 * Enter / 双击选区 确认截图
//                 * Esc 取消退出
//
// 架构预留：
//   1. 标注工具（文字/箭头/矩形/马赛克...）后续在此画布上实现，
//      编辑结果经 confirm() 合成到最终 pixmap 输出
//   2. F3 贴图：captured() 输出的 pixmap 交由 PinWindow 置顶显示
// ============================================================
class CaptureWindow : public QWidget
{
    Q_OBJECT
public:
    explicit CaptureWindow(QWidget *parent = nullptr);

    // 启动截图（捕获虚拟桌面画面并全屏显示）
    void startCapture();

signals:
    // 完成截图（选中区域画面）；null 表示取消
    void captured(const QPixmap &pixmap);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

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
    void confirm();  // 确认截图（Enter / 双击）
    void cancel();   // 取消截图（Esc）

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

    static constexpr int kMinSize = 8;   // 选区最小尺寸（像素）
    static constexpr int kHandle = 8;    // 控制点半边长（像素）

    // ===== 后续标注工具架构预留 =====
    // 工具枚举：Select / Text / Arrow / Rect / Mosaic ...
    // 标注项容器：QVector<AnnotationItem> m_annotations;
    // 待实现：在选区内绘制文字/箭头，随 confirm() 合成到输出 pixmap
};
