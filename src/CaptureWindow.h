#pragma once

#include <QWidget>
#include <QRubberBand>
#include <QPixmap>
#include <QPoint>

// 截图窗口：覆盖全屏，鼠标拖拽框选区域，确认后发出 captured 信号
class CaptureWindow : public QWidget
{
    Q_OBJECT
public:
    explicit CaptureWindow(QWidget *parent = nullptr);

    // 启动截图（捕获当前屏幕画面并全屏显示）
    void startCapture();

signals:
    // 截图完成（选中区域画面，为 null 表示取消）
    void captured(const QPixmap &pixmap);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QPixmap m_screen;          // 截取的屏幕画面
    QPoint m_origin;           // 拖拽起点（全局坐标）
    bool m_selecting = false;  // 是否正在框选
    QRect m_selection;         // 当前选区（全局坐标）
};
