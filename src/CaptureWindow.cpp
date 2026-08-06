#include "CaptureWindow.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTextOption>

CaptureWindow::CaptureWindow(QWidget *parent)
    : QWidget(parent)
{
    // 全屏无边框置顶窗口
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::Tool | Qt::BypassWindowManagerHint);
    setCursor(Qt::CrossCursor);
}

void CaptureWindow::startCapture()
{
    // 计算所有屏幕合并后的虚拟桌面区域
    const QList<QScreen *> screens = QGuiApplication::screens();
    QRect total;
    for (QScreen *s : screens)
        total = total.united(s->geometry());

    // 分别抓取每个屏幕并拼接到虚拟桌面画面（避免扩展屏黑屏）
    m_screen = QPixmap(total.size());
    m_screen.fill(Qt::black);
    {
        QPainter p(&m_screen);
        for (QScreen *s : screens) {
            const QPixmap shot = s->grabWindow(0);
            p.drawPixmap(s->geometry().topLeft() - total.topLeft(), shot);
        }
    }

    m_selecting = false;
    m_selection = QRect();
    setGeometry(total);
    show();
    activateWindow();
    raise();
    setFocus();
}

void CaptureWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    // 画屏幕画面
    p.drawPixmap(rect(), m_screen);

    // 半透明遮罩（选区外变暗）
    if (!m_selection.isEmpty()) {
        QPainterPath path;
        path.addRect(rect());
        path.addRect(m_selection);
        p.fillPath(path, QColor(0, 0, 0, 120));
        // 选区边框
        p.setPen(QPen(QColor(0, 150, 255), 1));
        p.drawRect(m_selection);
        // 尺寸提示
        p.drawText(m_selection.adjusted(4, 4, -4, -4),
                   QString("%1 × %2").arg(m_selection.width()).arg(m_selection.height()),
                   QTextOption(Qt::AlignTop | Qt::AlignLeft));
    }
}

void CaptureWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_origin = event->globalPosition().toPoint();
        m_selection = QRect(m_origin, QSize(0, 0));
        m_selecting = true;
    } else if (event->button() == Qt::RightButton) {
        // 右键取消
        m_selection = QRect();
        hide();
        emit captured(QPixmap());
    }
}

void CaptureWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_selecting)
        return;
    const QPoint cur = event->globalPosition().toPoint();
    m_selection = QRect(m_origin, cur).normalized();
    update();
}

void CaptureWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_selecting)
        return;
    m_selecting = false;

    // 区域过小视为取消
    if (m_selection.width() < 3 || m_selection.height() < 3) {
        m_selection = QRect();
        hide();
        emit captured(QPixmap());
        return;
    }

    // 裁剪选中区域（m_screen 以虚拟桌面左上角为原点，需减去 total 偏移）
    const QRect crop = m_selection.translated(-geometry().topLeft());
    const QPixmap result = m_screen.copy(crop);
    hide();
    emit captured(result);
}

void CaptureWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        m_selection = QRect();
        hide();
        emit captured(QPixmap());
    }
}
