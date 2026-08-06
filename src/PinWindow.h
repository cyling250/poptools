#pragma once

#include <QWidget>
#include <QPixmap>
#include <QPoint>

// ============================================================
// 贴图窗口（F3 功能，架构预留）
//
// 用途：将截图/图片以置顶悬浮窗形式贴到屏幕最前方，供参考。
// 预期交互：
//   * 置顶悬浮显示图片（WS_EX_TOPMOST 或 Qt 置顶窗口）
//   * 按住拖动移动位置
//   * 滚轮缩放
//   * 双击/右键关闭
//   * 支持多张贴图并存
//
// 当前状态：仅定义接口骨架，功能待后续实现。
// 接入点：MainWindow 中注册 F3 全局热键 → PinWindow::pin(image)
// ============================================================
class PinWindow : public QWidget
{
    Q_OBJECT
public:
    explicit PinWindow(QWidget *parent = nullptr);

    // 将图片贴到屏幕上（置顶显示）
    void pin(const QPixmap &pixmap);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QPixmap m_pixmap;      // 贴图内容
    QPoint m_dragOffset;   // 拖动时鼠标相对窗口左上角偏移
    bool m_dragging = false;
    qreal m_scale = 1.0;   // 缩放比例（预留）

    static constexpr int kMinWidth = 40;   // 最小缩放尺寸（预留）
    static constexpr int kMaxWidth = 4000; // 最大缩放尺寸（预留）
};
