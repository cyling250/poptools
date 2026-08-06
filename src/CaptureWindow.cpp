#include "CaptureWindow.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTextOption>
#include <QInputDialog>
#include <QFileDialog>
#include <QClipboard>
#include <QMessageBox>
#include <QStandardPaths>
#include <QLineEdit>
#include <QFontMetricsF>

#include "ToolBar.h"

CaptureWindow::CaptureWindow(QWidget *parent)
    : QWidget(parent)
{
    // 全屏无边框置顶窗口
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::Tool | Qt::BypassWindowManagerHint);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true); // 编辑态下实时反馈控制点光标

    // 工具栏（编辑态显示）
    m_toolbar = new ToolBar(this);
    m_toolbar->hide();
    connect(m_toolbar, &ToolBar::toolSelected, this, &CaptureWindow::onToolSelected);
    connect(m_toolbar, &ToolBar::colorSelected, this, &CaptureWindow::onColorSelected);
    connect(m_toolbar, &ToolBar::undoRequested, this, [this] {
        m_canvas.undo();
        m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
        update();
    });
    connect(m_toolbar, &ToolBar::redoRequested, this, [this] {
        m_canvas.redo();
        m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
        update();
    });
    connect(m_toolbar, &ToolBar::saveRequested, this, &CaptureWindow::onSave);
    connect(m_toolbar, &ToolBar::copyRequested, this, &CaptureWindow::onCopy);
    connect(m_toolbar, &ToolBar::confirmRequested, this, &CaptureWindow::confirm);
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
    m_canvas.clear();
    m_toolbar->hide();
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
            // 编辑态：绘制标注内容（选区局部坐标 → 画布坐标）
            p.save();
            p.translate(sel.topLeft());
            m_canvas.draw(p);
            // 正在绘制的草稿
            if (m_drawing)
                AnnotationCanvas::drawOne(p, m_draft);
            p.restore();

            // 蓝色 1.5pt 实线边框
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
            p.setPen(QPen(QColor(0, 120, 255), 1));
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

// ==================== 工具栏 ====================

void CaptureWindow::enterEditingState()
{
    m_state = State::Editing;
    m_activeHandle = Handle::None;
    m_moving = false;
    m_drawing = false;
    m_polyPts.clear();

    // 显示工具栏（底部居中）
    m_toolbar->adjustSize();
    m_toolbar->move((width() - m_toolbar->width()) / 2, height() - m_toolbar->height() - 8);
    m_toolbar->show();
    m_toolbar->raise();
    m_toolbar->setUndoRedoEnabled(false, false);
    update();
}

void CaptureWindow::exitCapture()
{
    m_state = State::Idle;
    m_selection = QRect();
    m_toolbar->hide();
    hide();
}

void CaptureWindow::onToolSelected(AnnotationCanvas::Tool t)
{
    m_canvas.tool = t;
    setCursor(t == AnnotationCanvas::Tool::Eraser ? Qt::PointingHandCursor
               : t == AnnotationCanvas::Tool::ColorPicker ? Qt::CrossCursor
               : Qt::CrossCursor);
}

void CaptureWindow::onColorSelected(const QColor &c)
{
    m_canvas.color = c;
}

void CaptureWindow::onSave()
{
    const QRect crop = m_selection.translated(-m_desktop.topLeft());
    QPixmap result = m_screen.copy(crop);
    {
        QPainter p(&result);
        p.translate(-crop.topLeft());
        m_canvas.draw(p);
    }
    const QString path = QFileDialog::getSaveFileName(
        this, "保存截图", QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        "PNG 图片 (*.png);;JPEG 图片 (*.jpg)");
    if (!path.isEmpty())
        result.save(path);
}

void CaptureWindow::onCopy()
{
    const QRect crop = m_selection.translated(-m_desktop.topLeft());
    QPixmap result = m_screen.copy(crop);
    {
        QPainter p(&result);
        p.translate(-crop.topLeft());
        m_canvas.draw(p);
    }
    QGuiApplication::clipboard()->setPixmap(result);
}

// ==================== 标注工具 ====================

QPointF CaptureWindow::toLocal(const QPoint &global) const
{
    return QPointF(global - m_selection.topLeft());
}

void CaptureWindow::startDraw(const QPointF &pos)
{
    switch (m_canvas.tool) {
    case AnnotationCanvas::Tool::Rect:
        m_draft.type = AnnotationType::Rect;
        m_draft.color = m_canvas.color;
        m_draft.width = m_canvas.width;
        m_draft.filled = m_canvas.filled;
        m_draft.rect = QRectF(pos, pos);
        m_drawing = true;
        break;
    case AnnotationCanvas::Tool::Ellipse:
        m_draft.type = AnnotationType::Ellipse;
        m_draft.color = m_canvas.color;
        m_draft.width = m_canvas.width;
        m_draft.filled = m_canvas.filled;
        m_draft.rect = QRectF(pos, pos);
        m_drawing = true;
        break;
    case AnnotationCanvas::Tool::Arrow:
        m_draft.type = AnnotationType::Arrow;
        m_draft.color = m_canvas.color;
        m_draft.width = m_canvas.width;
        m_draft.points = QPolygonF() << pos << pos;
        m_drawing = true;
        break;
    case AnnotationCanvas::Tool::Freehand:
        m_draft.type = AnnotationType::Freehand;
        m_draft.color = m_canvas.color;
        m_draft.width = m_canvas.width;
        m_draft.points = QPolygonF() << pos;
        m_drawing = true;
        break;
    case AnnotationCanvas::Tool::Polyline:
        // 折线：点击添加顶点，双击/回车结束
        m_polyPts.append(pos);
        break;
    case AnnotationCanvas::Tool::Eraser:
        m_canvas.eraseAt(pos);
        m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
        update();
        break;
    case AnnotationCanvas::Tool::Text: {
        // 弹输入框输入文本，回车确认
        bool ok = false;
        const QString text = QInputDialog::getText(
            this, "文本标注", "输入文本：", QLineEdit::Normal, QString(), &ok);
        if (ok && !text.isEmpty()) {
            AnnotationItem it;
            it.type = AnnotationType::Text;
            it.color = m_canvas.color;
            it.font = QFont("Microsoft YaHei", 14);
            it.font.setBold(true);
            it.text = text;
            it.rect = QRectF(pos, QSizeF(0, 0));
            // 估算文本大小
            QFontMetricsF fm(it.font);
            it.rect.setSize(QSizeF(fm.horizontalAdvance(text), fm.height()));
            m_canvas.addItem(it);
            m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
            update();
        }
        break;
    }
    case AnnotationCanvas::Tool::ColorPicker:
        pickColor(pos);
        break;
    }
}

void CaptureWindow::continueDraw(const QPointF &pos)
{
    if (!m_drawing)
        return;
    switch (m_canvas.tool) {
    case AnnotationCanvas::Tool::Rect:
    case AnnotationCanvas::Tool::Ellipse:
        m_draft.rect = QRectF(m_draft.rect.topLeft(), pos).normalized();
        break;
    case AnnotationCanvas::Tool::Arrow:
        if (m_draft.points.size() >= 2)
            m_draft.points[1] = pos;
        break;
    case AnnotationCanvas::Tool::Freehand:
        m_draft.points.append(pos);
        break;
    default:
        break;
    }
    update();
}

void CaptureWindow::finishDraw(const QPointF &pos)
{
    if (m_drawing) {
        // 过小图形忽略
        const bool tooSmall =
            (m_canvas.tool == AnnotationCanvas::Tool::Rect ||
             m_canvas.tool == AnnotationCanvas::Tool::Ellipse) &&
            (m_draft.rect.width() < 2 || m_draft.rect.height() < 2);
        if (!tooSmall) {
            m_canvas.addItem(m_draft);
            m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
        }
        m_drawing = false;
        update();
    }
}

void CaptureWindow::pickColor(const QPointF &pos)
{
    // 从屏幕画面中取色（全局坐标 → 桌面图像坐标）
    const QPoint imgPos = m_selection.topLeft() + pos.toPoint() - m_desktop.topLeft();
    const QImage img = m_screen.toImage();
    if (img.rect().contains(imgPos)) {
        m_pickedColor = img.pixelColor(imgPos);
        m_canvas.color = m_pickedColor;
        m_toolbar->setColor(m_pickedColor);
    }
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
        // 编辑态下优先处理标注工具（选区内点击）
        if (m_selection.contains(pos)) {
            startDraw(toLocal(pos));
            return;
        }
        // 选区外：控制点 / 移动 / 重新框选
        m_activeHandle = handleAt(pos);
        if (m_activeHandle != Handle::None) {
            m_handleStartPos = pos;
            m_handleStartSel = m_selection;
            setCursorFor(m_activeHandle);
            return;
        }
        if (m_selection.contains(pos)) {
            m_moving = true;
            m_moveOffset = pos - m_selection.topLeft();
            setCursor(Qt::SizeAllCursor);
            return;
        }
        // 选区外点击 → 重新开始框选
        m_state = State::Selecting;
        m_toolbar->hide();
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
        // 正在绘制标注
        if (m_drawing && event->buttons() & Qt::LeftButton) {
            continueDraw(toLocal(pos));
            return;
        }
        // 拖动控制点
        if (m_activeHandle != Handle::None && event->buttons() & Qt::LeftButton) {
            resizeSelection(m_activeHandle, pos);
            return;
        }
        // 整体移动选区（标注为局部坐标，随选区平移）
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
        if (m_selection.contains(pos))
            setCursor(Qt::CrossCursor);
        else
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
        enterEditingState();
        return;
    }

    if (m_state == State::Editing) {
        // 结束绘制
        if (m_drawing) {
            finishDraw(toLocal(event->globalPosition().toPoint()));
            return;
        }
        // 折线工具：单击已加顶点，释放时双击或按回车结束
        if (m_canvas.tool == AnnotationCanvas::Tool::Polyline
            && !m_polyPts.isEmpty() && m_selection.contains(event->globalPosition().toPoint())) {
            // 完成折线（双击结束由 mouseDoubleClick 处理）
        }
        m_activeHandle = Handle::None;
        m_moving = false;
    }
}

void CaptureWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (m_state == State::Editing && m_selection.contains(event->globalPosition().toPoint())) {
        // 折线工具：双击结束并提交折线
        if (m_canvas.tool == AnnotationCanvas::Tool::Polyline && m_polyPts.size() >= 2) {
            AnnotationItem it;
            it.type = AnnotationType::Polyline;
            it.color = m_canvas.color;
            it.width = m_canvas.width;
            it.points = QPolygonF::fromList(m_polyPts);
            m_canvas.addItem(it);
            m_polyPts.clear();
            m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
            update();
            return;
        }
        // 其他工具：双击选区完成截图
        confirm();
    }
}

// ==================== 键盘事件 ====================

void CaptureWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancel();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_state == State::Editing) {
            // 折线工具：回车结束折线
            if (m_canvas.tool == AnnotationCanvas::Tool::Polyline && m_polyPts.size() >= 2) {
                AnnotationItem it;
                it.type = AnnotationType::Polyline;
                it.color = m_canvas.color;
                it.width = m_canvas.width;
                it.points = QPolygonF::fromList(m_polyPts);
                m_canvas.addItem(it);
                m_polyPts.clear();
                m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
                update();
                return;
            }
            confirm();
        }
    }
}

void CaptureWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_toolbar->isVisible()) {
        m_toolbar->adjustSize();
        m_toolbar->move((width() - m_toolbar->width()) / 2, height() - m_toolbar->height() - 8);
    }
}

// ==================== 确认 / 取消 ====================

void CaptureWindow::confirm()
{
    if (m_selection.isEmpty())
        return;

    // 裁剪选中区域并合成标注
    const QRect crop = m_selection.translated(-m_desktop.topLeft());
    QPixmap result = m_screen.copy(crop);
    {
        QPainter p(&result);
        p.translate(-crop.topLeft());
        m_canvas.draw(p);
        if (m_drawing) {
            AnnotationCanvas::drawOne(p, m_draft);
            m_drawing = false;
        }
    }
    exitCapture();
    emit captured(result);
}

void CaptureWindow::cancel()
{
    exitCapture();
    emit captured(QPixmap());
}
