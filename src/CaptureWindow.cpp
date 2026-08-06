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
    setMouseTracking(true); // 编辑态下实时反馈控制点光标
}

void CaptureWindow::startCapture()
{
    // 计算所有屏幕合并后的虚拟桌面区域
    const QList<QScreen *> screens = QGuiApplication::screens();
    m_desktop = QRect();
    for (QScreen *s : screens)
        m_desktop = m_desktop.united(s->geometry());

    // 分别抓取每个屏幕并拼接到虚拟桌面画面（避免扩展屏黑屏）
    m_screen = QPixmap(m_desktop.size());
    m_screen.fill(Qt::black);
    {
        QPainter p(&m_screen);
        for (QScreen *s : screens) {
            const QPixmap shot = s->grabWindow(0);
            p.drawPixmap(s->geometry().topLeft() - m_desktop.topLeft(), shot);
        }
    }

    m_state = State::Selecting;
    m_selection = QRect();
    setGeometry(m_desktop);
    show();
    activateWindow();
    raise();
    setFocus();
}

// ==================== 绘制 ====================

void CaptureWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.drawPixmap(rect(), m_screen); // 底层为原始屏幕画面

    if (m_state == State::Idle)
        return;

    // 灰色蒙版：选区外变暗，选区内保持原图（高亮）
    if (!m_selection.isEmpty()) {
        QPainterPath mask;
        mask.addRect(rect());
        mask.addRect(m_selection.translated(-m_desktop.topLeft()));
        p.fillPath(mask, QColor(0, 0, 0, 110)); // 半透明灰蒙版

        // 选区（全局坐标 → 窗口坐标）
        const QRect sel = m_selection.translated(-m_desktop.topLeft());

        if (m_state == State::Selecting) {
            // 框选过程中：浅蓝虚线框
            p.setPen(QPen(QColor(0, 150, 255), 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(sel);
        } else {
            // 编辑态：蓝色 1.5pt 实线边框
            p.setPen(QPen(QColor(0, 120, 255), 1.5, Qt::SolidLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(sel);

            // 8 个控制点（白色方块 + 蓝色描边）
            const Handle handles[] = {
                Handle::TL, Handle::T, Handle::TR,
                Handle::R,  Handle::BR, Handle::B,
                Handle::BL, Handle::L
            };
            p.setBrush(Qt::white);
            for (Handle h : handles)
                p.drawRect(handleRect(h).translated(-m_desktop.topLeft()));
        }

        // 尺寸提示（选区左上角上方）
        p.setPen(Qt::white);
        p.drawText(sel.translated(4, -16),
                   QString("%1 × %2").arg(m_selection.width()).arg(m_selection.height()));
    }
}

// ==================== 控制点几何 ====================

QRect CaptureWindow::handleRect(Handle h) const
{
    QPoint c = m_selection.center();
    QPoint pt;
    switch (h) {
    case Handle::TL: pt = m_selection.topLeft(); break;
    case Handle::T:  pt = QPoint(c.x(), m_selection.top()); break;
    case Handle::TR: pt = m_selection.topRight(); break;
    case Handle::R:  pt = QPoint(m_selection.right(), c.y()); break;
    case Handle::BR: pt = m_selection.bottomRight(); break;
    case Handle::B:  pt = QPoint(c.x(), m_selection.bottom()); break;
    case Handle::BL: pt = m_selection.bottomLeft(); break;
    case Handle::L:  pt = QPoint(m_selection.left(), c.y()); break;
    case Handle::None: return QRect();
    }
    return QRect(pt - QPoint(kHandle, kHandle), QSize(2 * kHandle, 2 * kHandle));
}

CaptureWindow::Handle CaptureWindow::handleAt(const QPoint &pos) const
{
    const Handle handles[] = {
        Handle::TL, Handle::T, Handle::TR,
        Handle::R,  Handle::BR, Handle::B,
        Handle::BL, Handle::L
    };
    for (Handle h : handles) {
        if (handleRect(h).adjusted(-2, -2, 2, 2).contains(pos))
            return h;
    }
    return Handle::None;
}

void CaptureWindow::setCursorFor(Handle h)
{
    switch (h) {
    case Handle::TL: case Handle::BR: setCursor(Qt::SizeFDiagCursor); break;
    case Handle::TR: case Handle::BL: setCursor(Qt::SizeBDiagCursor); break;
    case Handle::T:  case Handle::B:  setCursor(Qt::SizeVerCursor); break;
    case Handle::L:  case Handle::R:  setCursor(Qt::SizeHorCursor); break;
    case Handle::None: setCursor(Qt::CrossCursor); break;
    }
}

void CaptureWindow::resizeSelection(Handle h, const QPoint &pos)
{
    QRect r = m_handleStartSel;
    switch (h) {
    case Handle::TL: r.setTopLeft(QPoint(qMin(pos.x(), r.right() - kMinSize),
                                          qMin(pos.y(), r.bottom() - kMinSize))); break;
    case Handle::T:  r.setTop(qMin(pos.y(), r.bottom() - kMinSize)); break;
    case Handle::TR: r.setTopRight(QPoint(qMax(pos.x(), r.left() + kMinSize),
                                          qMin(pos.y(), r.bottom() - kMinSize))); break;
    case Handle::R:  r.setRight(qMax(pos.x(), r.left() + kMinSize)); break;
    case Handle::BR: r.setBottomRight(QPoint(qMax(pos.x(), r.left() + kMinSize),
                                             qMax(pos.y(), r.top() + kMinSize))); break;
    case Handle::B:  r.setBottom(qMax(pos.y(), r.top() + kMinSize)); break;
    case Handle::BL: r.setBottomLeft(QPoint(qMin(pos.x(), r.right() - kMinSize),
                                            qMax(pos.y(), r.top() + kMinSize))); break;
    case Handle::L:  r.setLeft(qMin(pos.x(), r.right() - kMinSize)); break;
    case Handle::None: return;
    }
    m_selection = r.intersected(m_desktop);
    update();
}

// ==================== 鼠标事件 ====================

void CaptureWindow::mousePressEvent(QMouseEvent *event)
{
    const QPoint pos = event->globalPosition().toPoint();

    if (event->button() == Qt::RightButton) {
        cancel();
        return;
    }

    if (event->button() != Qt::LeftButton)
        return;

    if (m_state == State::Selecting) {
        m_origin = pos;
        m_selection = QRect(m_origin, QSize(0, 0));
        update();
        return;
    }

    if (m_state == State::Editing) {
        // 优先命中控制点 → 精调选区
        m_activeHandle = handleAt(pos);
        if (m_activeHandle != Handle::None) {
            m_handleStartPos = pos;
            m_handleStartSel = m_selection;
            setCursorFor(m_activeHandle);
            return;
        }
        // 选区内 → 整体移动
        if (m_selection.contains(pos)) {
            m_moving = true;
            m_moveOffset = pos - m_selection.topLeft();
            setCursor(Qt::SizeAllCursor);
            return;
        }
        // 选区外点击 → 重新开始框选
        m_state = State::Selecting;
        m_origin = pos;
        m_selection = QRect(m_origin, QSize(0, 0));
        update();
    }
}

void CaptureWindow::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->globalPosition().toPoint();

    if (m_state == State::Selecting && event->buttons() & Qt::LeftButton) {
        m_selection = QRect(m_origin, pos).normalized();
        update();
        return;
    }

    if (m_state == State::Editing) {
        // 拖动控制点
        if (m_activeHandle != Handle::None && event->buttons() & Qt::LeftButton) {
            resizeSelection(m_activeHandle, pos);
            return;
        }
        // 整体移动选区
        if (m_moving && event->buttons() & Qt::LeftButton) {
            QRect r = m_selection;
            r.moveTopLeft(pos - m_moveOffset);
            r = r.intersected(m_desktop);
            if (r.width() >= kMinSize && r.height() >= kMinSize) {
                m_selection = r;
                update();
            }
            return;
        }
        // 未按下：根据悬停位置更新光标
        setCursorFor(handleAt(pos));
    }
}

void CaptureWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (m_state == State::Selecting) {
        m_selection = QRect(m_origin, event->globalPosition().toPoint()).normalized();
        // 过小视为取消
        if (m_selection.width() < kMinSize || m_selection.height() < kMinSize) {
            cancel();
            return;
        }
        // 进入编辑态：可拖动控制点精调、移动选区，Esc 退出
        m_state = State::Editing;
        m_activeHandle = Handle::None;
        m_moving = false;
        update();
        return;
    }

    if (m_state == State::Editing) {
        m_activeHandle = Handle::None;
        m_moving = false;
    }
}

void CaptureWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton
        && m_state == State::Editing && m_selection.contains(event->globalPosition().toPoint())) {
        confirm();
    }
}

// ==================== 键盘事件 ====================

void CaptureWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancel();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_state == State::Editing)
            confirm();
    }
}

// ==================== 确认 / 取消 ====================

void CaptureWindow::confirm()
{
    if (m_selection.isEmpty())
        return;

    // 裁剪选中区域（m_screen 以虚拟桌面左上角为原点）
    // TODO(标注工具): 将 m_annotations 绘制到 result 上
    const QRect crop = m_selection.translated(-m_desktop.topLeft());
    const QPixmap result = m_screen.copy(crop);
    m_state = State::Idle;
    m_selection = QRect();
    hide();
    emit captured(result);
}

void CaptureWindow::cancel()
{
    m_state = State::Idle;
    m_selection = QRect();
    hide();
    emit captured(QPixmap());
}
