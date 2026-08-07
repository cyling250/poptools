#include "PinWindow.h"

#include <QGuiApplication>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>

// ============================================================
// 贴图窗口（F3 功能）
// 贴图内容随窗口显示、可拖动、滚轮缩放、双击关闭。
// 置顶层级由 Qt::WindowStaysOnTopHint 保证。
// 后续增强：多屏定位、透明度、点击穿透等。
// ============================================================

namespace snipaste {

PinWindow::PinWindow(QWidget *parent)
    : QWidget(parent)
{
    // 置顶无边框工具窗口
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose); // 关闭即销毁
}

void PinWindow::pin(const QPixmap &pixmap)
{
    m_pixmap = pixmap;
    m_scale = 1.0;
    // 初始尺寸 = 图片原始尺寸（限制在屏幕 80% 内）
    const QSize maxSize = QGuiApplication::primaryScreen()->availableGeometry().size() * 4 / 5;
    const QSize base = pixmap.size().boundedTo(maxSize);
    resize(base);
    show();
    raise();
}

void PinWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    // 贴图内容按缩放比例绘制（当前 m_scale 恒为 1）
    p.drawPixmap(rect(), m_pixmap);
}

void PinWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
}

void PinWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && event->buttons() & Qt::LeftButton)
        move(event->globalPosition().toPoint() - m_dragOffset);
}

void PinWindow::wheelEvent(QWheelEvent *event)
{
    // 滚轮缩放
    const qreal delta = event->angleDelta().y() / 1200.0;
    m_scale = qBound(0.2, m_scale + delta, 5.0);
    resize(size() * (1.0 + delta));
    update();
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    close();
}

} // namespace snipaste
