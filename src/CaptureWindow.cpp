#include "CaptureWindow.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTextOption>
#include <QFileDialog>
#include <QClipboard>
#include <QMessageBox>
#include <QStandardPaths>
#include <QLineEdit>
#include <QFontMetricsF>

#include "ToolBar.h"

// DPI：屏幕逻辑几何 → 物理像素几何（m_screen 坐标系基准）。
// Qt 6.8 的 QScreen 无 nativeGeometry()，按 Windows 每显示器 DPI 映射：
// 物理 = 屏幕逻辑原点×dpr + 屏幕内偏移×dpr
static QRect screenPhysicalRect(const QScreen *s)
{
    const qreal dpr = s->devicePixelRatio();
    const QRect lg = s->geometry();
    const QPoint tl(qRound(lg.left() * dpr), qRound(lg.top() * dpr));
    const QPoint br(qRound((lg.right() + 1) * dpr), qRound((lg.bottom() + 1) * dpr));
    return QRect(tl, QSize(br.x() - tl.x(), br.y() - tl.y()));
}

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
    connect(m_toolbar, &ToolBar::widthSelected, this, [this](qreal w) { m_canvas.width = w; });
    connect(m_toolbar, &ToolBar::filledToggled, this, [this](bool f) { m_canvas.filled = f; });
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

    // 画布内嵌文本输入框（PPT 风格：待输入框贴在点击位置）
    m_textEdit = new QLineEdit(this);
    m_textEdit->hide();
    m_textEdit->setStyleSheet(
        "QLineEdit { background: rgba(255,255,255,235); border: 1px solid #0078ff;"
        " border-radius: 3px; padding: 2px 4px; }");
    connect(m_textEdit, &QLineEdit::returnPressed, this, &CaptureWindow::commitTextEdit);
}

void CaptureWindow::startCapture()
{
    // 计算所有屏幕合并后的虚拟桌面区域（逻辑坐标：窗口/鼠标/选区均用逻辑坐标）
    const QList<QScreen *> screens = QGuiApplication::screens();
    m_desktop = QRect();
    for (QScreen *s : screens)
        m_desktop = m_desktop.united(s->geometry());

    // DPI 修复：grabWindow 返回的是物理像素图。此前按逻辑尺寸创建 m_screen，
    // 物理图被 drawPixmap 压缩 → 截图掉分辨率。现按物理分辨率拼接
    m_physDesktop = QRect();
    for (QScreen *s : screens)
        m_physDesktop = m_physDesktop.united(screenPhysicalRect(s));
    m_screen = QPixmap(m_physDesktop.size());
    m_screen.fill(Qt::black);
    {
        QPainter p(&m_screen);
        for (QScreen *s : screens) {
            QPixmap shot = s->grabWindow(0);
            shot.setDevicePixelRatio(1.0); // 物理像素 1:1 拼接（否则按 dpr 缩小）
            p.drawPixmap(screenPhysicalRect(s).topLeft() - m_physDesktop.topLeft(), shot);
        }
    }
    m_state = State::Selecting;
    m_selection = QRect();
    m_canvas.clear();
    m_canvas.tool = AnnotationCanvas::Tool::None; // 默认选择模式
    m_drawing = false;
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
    p.setRenderHint(QPainter::SmoothPixmapTransform); // 物理原图缩放到逻辑窗口时保持平滑
    p.drawPixmap(rect(), m_screen); // 底层为原始屏幕画面（物理分辨率）

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
            // 裁剪到选区内：防止标注拖出选区时，未填充内部透出灰蒙版显示为黑色
            p.setClipRect(sel);
            p.translate(sel.topLeft());
            m_canvas.draw(p);
            // 正在绘制的草稿
            if (m_drawing)
                AnnotationCanvas::drawOne(p, m_draft);
            // 选择模式：被选中标注的边框 + 控制点（选区局部坐标）
            if (m_selectedItem >= 0 && m_selectedItem < m_canvas.items.size()) {
                const AnnotationItem &selIt = m_canvas.items[m_selectedItem];
                const QRectF b = AnnotationCanvas::itemBounds(selIt);
                p.setPen(QPen(QColor(0, 200, 120), 1, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawRect(b);
                const Handle itemHandles[] = {
                    Handle::TL, Handle::T, Handle::TR,
                    Handle::R,  Handle::BR, Handle::B,
                    Handle::BL, Handle::L
                };
                p.setPen(QPen(QColor(0, 200, 120), 1));
                p.setBrush(Qt::white);
                for (Handle h : itemHandles)
                    p.drawRect(handleRect(b, h));
            }
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

        // 尺寸提示（物理分辨率 = 最终输出尺寸）
        p.setPen(Qt::white);
        const QSize phys = selectionToPhysical().size();
        p.drawText(sel.translated(4, -16),
                   QString("%1 × %2").arg(phys.width()).arg(phys.height()));
    }
}

// ==================== 控制点几何 ====================

QRectF CaptureWindow::handleRect(const QRectF &r, Handle h) const
{
    const QPointF c = r.center();
    QPointF pt;
    switch (h) {
    case Handle::TL: pt = r.topLeft(); break;
    case Handle::T:  pt = QPointF(c.x(), r.top()); break;
    case Handle::TR: pt = r.topRight(); break;
    case Handle::R:  pt = QPointF(r.right(), c.y()); break;
    case Handle::BR: pt = r.bottomRight(); break;
    case Handle::B:  pt = QPointF(c.x(), r.bottom()); break;
    case Handle::BL: pt = r.bottomLeft(); break;
    case Handle::L:  pt = QPointF(r.left(), c.y()); break;
    case Handle::None: return QRectF();
    }
    return QRectF(pt - QPointF(kHandle, kHandle), QSizeF(2 * kHandle, 2 * kHandle));
}

QRect CaptureWindow::handleRect(Handle h) const
{
    return handleRect(QRectF(m_selection), h).toRect();
}

CaptureWindow::Handle CaptureWindow::handleAt(const QRectF &b, const QPointF &pos) const
{
    const Handle handles[] = {
        Handle::TL, Handle::T, Handle::TR,
        Handle::R,  Handle::BR, Handle::B,
        Handle::BL, Handle::L
    };
    for (Handle h : handles) {
        if (handleRect(b, h).adjusted(-2, -2, 2, 2).contains(pos))
            return h;
    }
    return Handle::None;
}

CaptureWindow::Handle CaptureWindow::handleAt(const QPoint &pos) const
{
    return handleAt(QRectF(m_selection), QPointF(pos));
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

QRectF CaptureWindow::resizeRectF(QRectF r, Handle h, const QPointF &pos, qreal minSize) const
{
    switch (h) {
    case Handle::TL: r.setTopLeft(QPointF(qMin(pos.x(), r.right() - minSize),
                                          qMin(pos.y(), r.bottom() - minSize))); break;
    case Handle::T:  r.setTop(qMin(pos.y(), r.bottom() - minSize)); break;
    case Handle::TR: r.setTopRight(QPointF(qMax(pos.x(), r.left() + minSize),
                                           qMin(pos.y(), r.bottom() - minSize))); break;
    case Handle::R:  r.setRight(qMax(pos.x(), r.left() + minSize)); break;
    case Handle::BR: r.setBottomRight(QPointF(qMax(pos.x(), r.left() + minSize),
                                              qMax(pos.y(), r.top() + minSize))); break;
    case Handle::B:  r.setBottom(qMax(pos.y(), r.top() + minSize)); break;
    case Handle::BL: r.setBottomLeft(QPointF(qMin(pos.x(), r.right() - minSize),
                                             qMax(pos.y(), r.top() + minSize))); break;
    case Handle::L:  r.setLeft(qMin(pos.x(), r.right() - minSize)); break;
    case Handle::None: break;
    }
    return r;
}

void CaptureWindow::resizeSelection(Handle h, const QPoint &pos)
{
    const QRectF n = resizeRectF(QRectF(m_handleStartSel), h, QPointF(pos), kMinSize);
    m_selection = n.toRect().intersected(m_desktop);
    update();
}

// ==================== 工具栏 ====================

void CaptureWindow::enterEditingState()
{
    m_state = State::Editing;
    m_activeHandle = Handle::None;
    m_moving = false;
    m_drawing = false;
    m_selectedItem = -1;
    m_itemMoving = false;
    m_itemActiveHandle = Handle::None;
    if (m_textEditing)
        commitTextEdit();

    // 显示工具栏（贴画布下方靠右）
    positionToolbar();
    m_toolbar->show();
    m_toolbar->raise();
    m_toolbar->setUndoRedoEnabled(false, false);
    m_toolbar->setTool(m_canvas.tool);
    update();
}

void CaptureWindow::positionToolbar()
{
    m_toolbar->adjustSize();
    const QRect sel = m_selection.translated(-m_desktop.topLeft());
    // 靠右对齐：工具栏右缘贴画布右缘（含 8px 外边距）
    const int x = sel.right() + 8 - m_toolbar->width();
    const int y = sel.bottom() + 8;
    m_toolbar->move(qBound(0, x, width() - m_toolbar->width()),
                    qBound(0, y, height() - m_toolbar->height()));
}

void CaptureWindow::exitCapture()
{
    m_state = State::Idle;
    m_selection = QRect();
    m_toolbar->hide();
    m_selectedItem = -1;
    m_itemMoving = false;
    m_itemActiveHandle = Handle::None;
    m_textEditing = false;
    m_textEditItem = -1;
    m_textEdit->hide();
    hide();
}

void CaptureWindow::onToolSelected(AnnotationCanvas::Tool t)
{
    // 切换工具前先保存/删除待输入文字
    if (m_textEditing)
        commitTextEdit();
    m_canvas.tool = t;
    m_drawing = false;
    m_selectedItem = -1; // 切换工具取消标注选中
    m_itemMoving = false;
    m_itemActiveHandle = Handle::None;
    // 光标反馈
    switch (t) {
    case AnnotationCanvas::Tool::Eraser: setCursor(Qt::PointingHandCursor); break;
    case AnnotationCanvas::Tool::None:   setCursor(Qt::SizeAllCursor); break; // 选择模式：可拖动画布
    default:                             setCursor(Qt::CrossCursor); break;
    }
}

void CaptureWindow::onColorSelected(const QColor &c)
{
    m_canvas.color = c;
}

// ==================== DPI 坐标映射 ====================
// 窗口/鼠标/选区均为 Qt 逻辑坐标，而 m_screen 为物理像素（grabWindow 原图）。
// 输出（保存/复制/确认）时需把逻辑选区换算为物理矩形，并把标注等比放大。

QPoint CaptureWindow::toPhysical(const QPoint &logicalGlobal) const
{
    QScreen *s = nullptr;
    for (QScreen *sc : QGuiApplication::screens()) {
        if (sc->geometry().contains(logicalGlobal)) {
            s = sc;
            break;
        }
    }
    if (!s)
        s = QGuiApplication::primaryScreen();
    const qreal dpr = s->devicePixelRatio();
    const QRect lg = s->geometry();
    // 与 screenPhysicalRect 一致：屏幕物理原点 + 屏幕内逻辑偏移×dpr
    return QPoint(qRound(lg.left() * dpr) + qRound((logicalGlobal.x() - lg.left()) * dpr),
                  qRound(lg.top() * dpr) + qRound((logicalGlobal.y() - lg.top()) * dpr));
}

QRect CaptureWindow::selectionToPhysical() const
{
    // 选区为逻辑全局坐标（Qt 矩形右/下边界含端点）→ 物理像素矩形
    const QPoint tl = toPhysical(m_selection.topLeft());
    const QPoint br = toPhysical(m_selection.bottomRight() + QPoint(1, 1));
    return QRect(tl, br - QPoint(1, 1));
}

void CaptureWindow::onSave()
{
    if (m_textEditing)
        commitTextEdit();
    // DPI：输出为物理分辨率（逻辑选区 → 物理矩形裁剪）
    const QRect crop = selectionToPhysical().translated(-m_physDesktop.topLeft());
    QPixmap result = m_screen.copy(crop);
    {
        QPainter p(&result);
        // 标注为选区局部逻辑坐标；结果图为物理像素 → 等比放大
        const qreal k = qreal(result.width()) / qMax(1, m_selection.width());
        p.scale(k, k);
        m_canvas.draw(p);
    }
    const QString path = QFileDialog::getSaveFileName(
        this, "保存截图", QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        "PNG 图片 (*.png);;JPEG 图片 (*.jpg)");
    if (path.isEmpty())
        return; // 用户取消对话框 → 留在编辑界面
    result.save(path);
    exitCapture(); // 保存后退出截图界面（显式操作才退出，防误触）
}

void CaptureWindow::onCopy()
{
    if (m_textEditing)
        commitTextEdit();
    // DPI：输出为物理分辨率
    const QRect crop = selectionToPhysical().translated(-m_physDesktop.topLeft());
    QPixmap result = m_screen.copy(crop);
    {
        QPainter p(&result);
        const qreal k = qreal(result.width()) / qMax(1, m_selection.width());
        p.scale(k, k);
        m_canvas.draw(p);
    }
    QGuiApplication::clipboard()->setPixmap(result);
    exitCapture(); // 复制后退出截图界面（显式操作才退出，防误触）
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
    case AnnotationCanvas::Tool::Ellipse:
        m_draftOrigin = pos; // 固定拖动锚点（按下点），避免反向拖动时随鼠标漂移
        m_draft.type = (m_canvas.tool == AnnotationCanvas::Tool::Rect)
                           ? AnnotationType::Rect : AnnotationType::Ellipse;
        m_draft.color = m_canvas.color;
        m_draft.width = m_canvas.width;
        m_draft.filled = m_canvas.filled;
        m_draft.rect = QRectF(pos, pos);
        m_drawing = true;
        break;
    case AnnotationCanvas::Tool::Line:
    case AnnotationCanvas::Tool::Arrow:
        m_draft.type = (m_canvas.tool == AnnotationCanvas::Tool::Line)
                           ? AnnotationType::Line : AnnotationType::Arrow;
        m_draft.color = m_canvas.color;
        m_draft.width = m_canvas.width;
        m_draft.points = QPolygonF() << pos << pos;
        m_drawing = true;
        break;
    case AnnotationCanvas::Tool::Pencil:
        m_draft.type = AnnotationType::Freehand;
        m_draft.color = m_canvas.color;
        m_draft.width = m_canvas.width;
        m_draft.points = QPolygonF() << pos;
        m_drawing = true;
        break;
    case AnnotationCanvas::Tool::Doodle:
        m_draft.type = AnnotationType::Doodle;
        m_draft.color = m_canvas.color;
        m_draft.color.setAlpha(100); // 半透明：高亮 / 荧光笔效果
        m_draft.width = m_canvas.width;
        m_draft.points = QPolygonF() << pos;
        m_drawing = true;
        break;
    case AnnotationCanvas::Tool::Mosaic:
        m_draft.type = AnnotationType::Mosaic;
        m_draft.width = m_canvas.width; // 笔刷直径
        m_draft.points = QPolygonF() << pos;
        m_drawing = true;
        buildMosaicImage();
        break;
    case AnnotationCanvas::Tool::Eraser:
        m_canvas.eraseAt(pos);
        m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
        update();
        break;
    case AnnotationCanvas::Tool::Text:
        beginTextEdit(pos, -1); // 画布内嵌待输入框（PPT 风格：空删 / 非空存）
        break;
    case AnnotationCanvas::Tool::None:
        break; // 选择模式：由调用方处理拖动画布
    }
}

void CaptureWindow::continueDraw(const QPointF &pos)
{
    if (!m_drawing)
        return;
    switch (m_canvas.tool) {
    case AnnotationCanvas::Tool::Rect:
    case AnnotationCanvas::Tool::Ellipse:
        // 固定按下点为锚点（修复：原先取 m_draft.rect.topLeft()，反向拖动时锚点随鼠标漂移）
        m_draft.rect = QRectF(m_draftOrigin, pos).normalized();
        break;
    case AnnotationCanvas::Tool::Line:
    case AnnotationCanvas::Tool::Arrow:
        if (m_draft.points.size() >= 2)
            m_draft.points[1] = pos;
        break;
    case AnnotationCanvas::Tool::Pencil:
    case AnnotationCanvas::Tool::Doodle:
        m_draft.points.append(pos);
        break;
    case AnnotationCanvas::Tool::Mosaic:
        m_draft.points.append(pos);
        buildMosaicImage(); // 笔刷涂抹：沿笔迹实时生成马赛克
        break;
    default:
        break;
    }
    update();
}

void CaptureWindow::finishDraw(const QPointF &pos)
{
    if (m_drawing) {
        const bool tooSmall =
            (m_canvas.tool == AnnotationCanvas::Tool::Rect ||
             m_canvas.tool == AnnotationCanvas::Tool::Ellipse) &&
            (m_draft.rect.width() < 2 || m_draft.rect.height() < 2);
        if (!tooSmall) {
            // 马赛克：基于原始屏幕画面生成笔刷足迹的像素块效果
            if (m_canvas.tool == AnnotationCanvas::Tool::Mosaic)
                buildMosaicImage();
            m_canvas.addItem(m_draft);
            m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
        }
        m_drawing = false;
        update();
    }
}

// 马赛克笔刷：沿笔刷足迹（折线 + 圆头）像素化，足迹以外区域保持透明
void CaptureWindow::buildMosaicImage()
{
    if (m_draft.points.isEmpty())
        return;

    const qreal r = qMax(2.0, m_draft.width / 2.0); // 笔刷半径
    // 足迹外接矩形（选区局部坐标），限制在选区内
    QRectF bounds;
    for (const QPointF &pt : m_draft.points)
        bounds = bounds.united(QRectF(pt - QPointF(r, r), QSizeF(2 * r, 2 * r)));
    const QRect local = bounds.toAlignedRect()
                            .adjusted(-1, -1, 1, 1)
                            .intersected(QRect(QPoint(0, 0), m_selection.size()));
    if (local.isEmpty())
        return;

    // 截取屏幕源区域并整体像素化（m_screen 为物理像素 → 换算物理矩形）
    const QRect global = local.translated(m_selection.topLeft()).intersected(m_desktop);
    if (global.isEmpty())
        return;
    const QRect physGlobal(QRect(toPhysical(global.topLeft()),
                                 toPhysical(global.bottomRight() + QPoint(1, 1)) - QPoint(1, 1)));
    if (physGlobal.isEmpty())
        return;
    const QPixmap region = m_screen.copy(physGlobal.translated(-m_physDesktop.topLeft()));
    const int block = 8; // 马赛克块大小（物理像素）
    if (region.width() < block || region.height() < block)
        return;
    QImage src = region.toImage();
    QImage small = src.scaled(qMax(1, src.width() / block), qMax(1, src.height() / block),
                              Qt::IgnoreAspectRatio, Qt::FastTransformation);
    // 必须转成带 alpha 的格式再合成：RGB32 无 alpha 通道，DestinationIn 会按源
    // alpha 把 RGB 乘成 0（纯黑）且图像仍不透明 → 表现为"黑色填充矩形+透明路径"
    QImage mosaic = small.scaled(src.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation)
                        .convertToFormat(QImage::Format_ARGB32_Premultiplied);

    // 笔刷足迹 mask（物理像素，与 mosaic 同尺寸）：保留沿线的圆头笔刷区域（其余透明）
    QImage mask(physGlobal.size(), QImage::Format_ARGB32);
    mask.fill(Qt::transparent);
    {
        QPainter p(&mask);
        p.setRenderHint(QPainter::Antialiasing);
        // 选区局部逻辑坐标 → 物理像素
        p.scale(qreal(physGlobal.width()) / qMax(1, local.width()),
                qreal(physGlobal.height()) / qMax(1, local.height()));
        p.setPen(QPen(Qt::white, m_draft.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::white);
        const QPointF off = QPointF(local.topLeft()); // 选区局部 → 区域局部
        QPolygonF pts;
        for (const QPointF &pt : m_draft.points)
            pts << pt - off;
        p.drawPolyline(pts);
        // 单点 / 端点处补圆点，保证笔刷为圆头
        for (const QPointF &pt : pts)
            p.drawEllipse(pt, r, r);
    }
    {
        QPainter p(&mosaic);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.drawImage(0, 0, mask);
    }

    m_draft.mosaicImage = mosaic;
    m_draft.rect = QRectF(local);
}

// ==================== 文字内嵌输入框（PPT 风格） ====================

void CaptureWindow::beginTextEdit(const QPointF &local, int itemIndex)
{
    // 停止其它交互
    m_itemMoving = false;
    m_itemActiveHandle = Handle::None;

    m_textEditItem = itemIndex;
    m_textEditPos = local;

    QFont f("Microsoft YaHei", 14);
    f.setBold(true);
    QPointF boxPos = local;
    if (itemIndex >= 0) {
        const AnnotationItem &it = m_canvas.items[itemIndex];
        if (it.type != AnnotationType::Text)
            return;
        f = it.font;
        m_textEdit->setText(it.text);
        boxPos = it.rect.topLeft(); // 待输入框贴在原文字位置
        m_selectedItem = itemIndex; // 重编辑的文本保持选中态
    } else {
        m_textEdit->clear();
        m_selectedItem = -1;
    }
    m_textEdit->setFont(f);

    // 定位：选区局部坐标 → 全局坐标 → 窗口坐标
    const QPointF global = boxPos + m_selection.topLeft();
    const QPoint win = (global - QPointF(m_desktop.topLeft())).toPoint();
    QFontMetricsF fm(f);
    m_textEdit->setGeometry(QRect(win, QSize(240, int(fm.height()) + 10)));
    m_textEdit->show();
    m_textEdit->raise();
    m_textEdit->setFocus();
    m_textEdit->selectAll();
    m_textEditing = true;
    update();
}

void CaptureWindow::commitTextEdit()
{
    if (!m_textEditing)
        return;
    const QString text = m_textEdit->text().trimmed();
    const int itemIdx = m_textEditItem;
    const QPointF localPos = m_textEditPos;
    m_textEdit->hide();
    m_textEdit->clear();
    m_textEditing = false;
    m_textEditItem = -1;

    // 重编辑期间原标注可能已被撤销/删除 → 直接丢弃本次编辑
    if (itemIdx >= 0 && itemIdx >= m_canvas.items.size()) {
        update();
        return;
    }

    if (text.isEmpty()) {
        // 无输入：删除文本框（新建则丢弃；重编辑则删除该标注）
        if (itemIdx >= 0) {
            m_canvas.pushSnapshot();
            m_canvas.removeItem(itemIdx);
            m_selectedItem = -1;
        }
        update();
        return;
    }

    if (itemIdx >= 0) {
        // 重编辑：更新文字内容与框尺寸（颜色/字号保持原有）
        AnnotationItem it = m_canvas.items[itemIdx];
        m_canvas.pushSnapshot();
        it.text = text;
        QFontMetricsF fm(it.font);
        it.rect = QRectF(it.rect.topLeft(), QSizeF(fm.horizontalAdvance(text), fm.height()));
        m_canvas.setItem(itemIdx, it);
    } else {
        // 新建文字标注
        AnnotationItem it;
        it.type = AnnotationType::Text;
        it.color = m_canvas.color;
        it.font = m_textEdit->font();
        it.text = text;
        QFontMetricsF fm(it.font);
        it.rect = QRectF(localPos, QSizeF(fm.horizontalAdvance(text), fm.height()));
        m_canvas.addItem(it);
    }
    m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
    update();
}

// ==================== 选择模式：标注项拖动 / 缩放 ====================

void CaptureWindow::moveSelectedItem(const QPointF &local)
{
    if (m_selectedItem < 0 || m_selectedItem >= m_canvas.items.size())
        return;
    const QPointF delta = local - m_itemStartPos;
    if (!m_itemSnapshotted) {
        m_canvas.pushSnapshot(); // 首笔变化才压撤销快照，避免无操作也产生撤销步骤
        m_itemSnapshotted = true;
    }
    AnnotationItem it = m_itemStartGeom;
    it.rect = m_itemStartBounds.translated(delta);
    for (QPointF &pt : it.points)
        pt += delta;
    m_canvas.setItem(m_selectedItem, it);
    update();
}

void CaptureWindow::resizeSelectedItem(const QPointF &local)
{
    if (m_selectedItem < 0 || m_selectedItem >= m_canvas.items.size())
        return;
    const QRectF newBounds = resizeRectF(m_itemStartBounds, m_itemActiveHandle, local, kMinSize)
                                 .normalized();
    if (newBounds.isEmpty())
        return;
    if (!m_itemSnapshotted) {
        m_canvas.pushSnapshot();
        m_itemSnapshotted = true;
    }
    m_canvas.setItem(m_selectedItem, AnnotationCanvas::scaledItem(m_itemStartGeom, newBounds));
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
        // 正在输入文字时，点击其它位置先提交（空删 / 非空存），再处理本次点击
        if (m_textEditing)
            commitTextEdit();

        // 选择模式（None）且点在选区内：标注项选中 / 拖动 / 缩放，或拖动选区
        if (m_canvas.tool == AnnotationCanvas::Tool::None && m_selection.contains(pos)) {
            const QPointF local = toLocal(pos);
            // 0) 选区自身的控制点优先（与标注项控制点互斥）
            const Handle selHandle = handleAt(pos);
            if (selHandle != Handle::None) {
                m_activeHandle = selHandle;
                m_handleStartPos = pos;
                m_handleStartSel = m_selection;
                m_selectedItem = -1;
                setCursorFor(selHandle);
                return;
            }
            // 1) 已有选中项：点其控制点 → 开始缩放
            if (m_selectedItem >= 0 && m_selectedItem < m_canvas.items.size()) {
                const AnnotationItem &sel = m_canvas.items[m_selectedItem];
                const QRectF b = AnnotationCanvas::itemBounds(sel);
                const Handle h = handleAt(b, local);
                if (h != Handle::None) {
                    m_itemActiveHandle = h;
                    m_itemStartPos = local;
                    m_itemStartBounds = b;
                    m_itemStartGeom = sel;
                    m_itemSnapshotted = false;
                    setCursorFor(h);
                    return;
                }
                m_selectedItem = -1; // 不在选中项控制点上 → 取消选中（下续可能点选新标注）
            }
            // 2) 点选标注项 → 选中并准备拖动
            const int idx = m_canvas.itemAt(local);
            if (idx >= 0) {
                m_selectedItem = idx;
                const AnnotationItem &sel = m_canvas.items[idx];
                m_itemMoving = true;
                m_itemStartPos = local;
                m_itemStartBounds = AnnotationCanvas::itemBounds(sel);
                m_itemStartGeom = sel;
                m_itemSnapshotted = false;
                setCursor(Qt::SizeAllCursor);
                update();
                return;
            }
            // 3) 无标注 → 拖动整个选区
            m_moving = true;
            m_moveOffset = pos - m_selection.topLeft();
            setCursor(Qt::SizeAllCursor);
            return;
        }
        // 工具模式下：选区内开始绘制
        if (m_canvas.tool != AnnotationCanvas::Tool::None && m_selection.contains(pos)) {
            startDraw(toLocal(pos));
            return;
        }

        // 选区外：控制点 / 移动（单击不再重画，防误触；重画由双击选区外触发）
        m_selectedItem = -1;
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
        // 选区外单击：忽略（防止误触退出编辑态）—— 双击选区外才重新框选
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
        // 拖动选中标注的控制点（缩放）
        if (m_itemActiveHandle != Handle::None && event->buttons() & Qt::LeftButton) {
            resizeSelectedItem(toLocal(pos));
            return;
        }
        // 拖动选中标注（移动）
        if (m_itemMoving && event->buttons() & Qt::LeftButton) {
            moveSelectedItem(toLocal(pos));
            return;
        }
        // 拖动选区控制点
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
                positionToolbar();
                update();
            }
            return;
        }
        // 未按下：光标反馈
        if (m_canvas.tool == AnnotationCanvas::Tool::None) {
            const QPointF local = toLocal(pos);
            const bool overSelHandle = handleAt(pos) != Handle::None;
            if (!m_selection.contains(pos) || overSelHandle) {
                setCursorFor(handleAt(pos));
            } else if (m_selectedItem >= 0 && m_selectedItem < m_canvas.items.size()) {
                // 选中标注：控制点光标 / 项内移动光标
                const QRectF b = AnnotationCanvas::itemBounds(m_canvas.items[m_selectedItem]);
                const Handle h = handleAt(b, local);
                if (h != Handle::None)
                    setCursorFor(h);
                else
                    setCursor(b.adjusted(-4, -4, 4, 4).contains(local)
                                  ? Qt::SizeAllCursor : Qt::ArrowCursor);
            } else {
                setCursor(m_canvas.hitTest(local) ? Qt::ArrowCursor : Qt::SizeAllCursor);
            }
        } else if (m_selection.contains(pos)) {
            setCursor(m_canvas.tool == AnnotationCanvas::Tool::Eraser
                          ? Qt::PointingHandCursor : Qt::CrossCursor);
        } else {
            setCursorFor(handleAt(pos));
        }
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
        m_activeHandle = Handle::None;
        m_moving = false;
        m_itemActiveHandle = Handle::None;
        m_itemMoving = false; // 选中标注保持选中（便于连续操作）
    }
}

void CaptureWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (m_state != State::Editing)
        return;
    const QPoint pos = event->globalPosition().toPoint();
    if (m_selection.contains(pos)) {
        // 选择模式：双击文字标注 → 重新编辑
        if (m_canvas.tool == AnnotationCanvas::Tool::None) {
            const QPointF local = toLocal(pos);
            const int idx = m_canvas.itemAt(local);
            if (idx >= 0 && m_canvas.items[idx].type == AnnotationType::Text) {
                beginTextEdit(local, idx);
                return;
            }
        }
        // 选区内其它双击：忽略（防误触——退出截图仅通过 Esc / Enter / 完成、复制、保存按钮）
        return;
    }
    // 双击选区外 → 重新开始框选（重画；单击选区外不再退出编辑态，防误触）
    m_state = State::Selecting;
    m_toolbar->hide();
    m_selectedItem = -1;
    m_itemMoving = false;
    m_itemActiveHandle = Handle::None;
    m_origin = pos;
    m_selection = QRect(m_origin, QSize(0, 0));
    update();
}

// ==================== 键盘事件 ====================

void CaptureWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_textEditing) {
            m_textEdit->clear(); // 空文本提交 → 删除待输入框 / 标注
            commitTextEdit();
            return;
        }
        cancel();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_state == State::Editing && !m_textEditing)
            confirm(); // 输入框内的 Enter 已由 returnPressed 提交
    }
}

void CaptureWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_toolbar->isVisible())
        positionToolbar();
}

// ==================== 确认 / 取消 ====================

void CaptureWindow::confirm()
{
    if (m_selection.isEmpty())
        return;
    if (m_textEditing)
        commitTextEdit();

    // 裁剪选中区域并合成标注（输出为物理分辨率）
    const QRect crop = selectionToPhysical().translated(-m_physDesktop.topLeft());
    QPixmap result = m_screen.copy(crop);
    {
        QPainter p(&result);
        const qreal k = qreal(result.width()) / qMax(1, m_selection.width());
        p.scale(k, k);
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
