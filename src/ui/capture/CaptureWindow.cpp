#include "CaptureWindow.h"
#include "ToolBar.h"
#include "core/annotation/AnnotationPainter.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTextOption>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QFileDialog>
#include <QClipboard>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextEdit>
#include <QFontMetricsF>

namespace snipaste
{

    // 屏幕逻辑几何 → 物理像素几何（m_screen 坐标系基准）。
    // DPI：窗口/鼠标/选区均为 Qt 逻辑坐标，而截屏画面为物理像素（grabWindow 原图）。
    // 输出（保存/复制/确认）时需把逻辑选区换算为物理矩形，并把标注等比放大。
    // 该映射统一在 ScreenCapturer 中实现。

    CaptureWindow::CaptureWindow(QWidget *parent)
        : QWidget(parent)
    {
        // 全屏无边框置顶窗口
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::BypassWindowManagerHint);
        setCursor(Qt::CrossCursor);
        setMouseTracking(true); // 编辑态下实时反馈控制点光标

        // 工具栏（单例，编辑态显示）
        m_toolbar = ToolBar::create(this);
        m_toolbar->hide();
        connect(m_toolbar, &ToolBar::toolSelected, this, &CaptureWindow::onToolSelected);
        connect(m_toolbar, &ToolBar::colorSelected, this, &CaptureWindow::onColorSelected);
        connect(m_toolbar, &ToolBar::widthSelected, this, [this](qreal w)
                {
        if (m_textEditing)
            commitTextEdit(); // 点击框外（工具栏）结束输入
        m_canvas.width = w; });
        connect(m_toolbar, &ToolBar::filledToggled, this, [this](bool f)
                {
        if (m_textEditing)
            commitTextEdit();
        m_canvas.filled = f; });
        connect(m_toolbar, &ToolBar::fontSizeSelected, this, &CaptureWindow::onFontSizeSelected);
        connect(m_toolbar, &ToolBar::undoRequested, this, [this]
                {
        if (m_textEditing)
            commitTextEdit();
        m_canvas.undo();
        m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
        update(); });
        connect(m_toolbar, &ToolBar::redoRequested, this, [this]
                {
        if (m_textEditing)
            commitTextEdit();
        m_canvas.redo();
        m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
        update(); });
        connect(m_toolbar, &ToolBar::saveRequested, this, &CaptureWindow::onSave);
        connect(m_toolbar, &ToolBar::copyRequested, this, &CaptureWindow::onCopy);
        connect(m_toolbar, &ToolBar::ocrRequested, this, &CaptureWindow::onOcrRequested);
        connect(&m_ocr, &OcrService::ocrBoxesFinished, this, &CaptureWindow::onOcrBoxes);

        // 工具上下文：注入画板 / 屏幕 / 工具栏引用与窗口回调（工具与窗口解耦）
        m_ctx.canvas = &m_canvas;
        m_ctx.cap = &m_cap;
        m_ctx.toolbar = m_toolbar;
        m_ctx.requestUpdate = [this]
        { update(); };
        m_ctx.stateChanged = [this]
        {
            m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
        };
        m_ctx.buildMosaic = [this](const QPolygonF &points, qreal width)
        {
            return buildMosaicItem(points, width);
        };
        m_ctx.beginText = [this](const QPointF &local, int existing)
        {
            beginTextEdit(local, existing);
        };

        // 画布内嵌文本输入框（多行）：透明底 + 虚线边框，尺寸随内容自适应；
        // Enter/Shift+Enter 均为换行，输入结束方式为鼠标点击框外区域（非回车）
        m_textEdit = new QTextEdit(this);
        m_textEdit->hide();
        m_textEdit->setLineWrapMode(QTextEdit::NoWrap); // 不自动换行：宽=最长行，高=总行高
        m_textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textEdit->document()->setDocumentMargin(0); // 去除文档内边距，尺寸即文本自然尺寸
        connect(m_textEdit, &QTextEdit::textChanged, this, [this]
                {
        adjustTextEditSize();   // 输入内容/字号变化 → 文本框实时跟随
        updateFontSizeDisplay(); });
        connect(m_textEdit, &QTextEdit::cursorPositionChanged, this, [this]
                {
                    if (m_textEditing)
                        updateFontSizeDisplay(); // 光标移动/选区变化 → 字号栏同步
                });
        connect(m_textEdit, &QTextEdit::selectionChanged, this, [this]
                {
        if (m_textEditing)
            updateFontSizeDisplay(); });

        // ESC 二次退出提示：2 秒后消退（重新按两次才会退出）
        m_escTimer.setSingleShot(true);
        connect(&m_escTimer, &QTimer::timeout, this, [this]
                {
        m_escArmed = false;
        update(); });
    }

    void CaptureWindow::startCapture()
    {
        // 捕获所有屏幕拼接的物理分辨率画面（多屏扩展无黑屏），
        // 并记录逻辑/物理虚拟桌面几何（窗口/鼠标/选区均用逻辑坐标）
        m_cap = ScreenCapturer::capture();

        m_state = State::Selecting;
        m_selection = QRect();
        m_canvas.clear();
        m_canvas.tool = Tool::None; // 默认选择模式
        m_drawing = false;
        m_tool.reset();     // 清空上一轮工具实例
        m_escArmed = false; // 重置 ESC 二次退出提示
        m_escTimer.stop();
        m_toolbar->hide();
        setGeometry(m_cap.desktop);
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
        p.drawPixmap(rect(), m_cap.screen);               // 底层为原始屏幕画面（物理分辨率）

        if (m_state == State::Idle)
            return;

        // 灰色蒙版：选区外变暗，选区内保持原图（高亮）
        if (!m_selection.isEmpty())
        {
            QPainterPath mask;
            mask.addRect(rect());
            mask.addRect(m_selection.translated(-m_cap.desktop.topLeft()));
            p.fillPath(mask, QColor(0, 0, 0, 110)); // 半透明灰蒙版

            // 选区（全局坐标 → 窗口坐标）
            const QRect sel = m_selection.translated(-m_cap.desktop.topLeft());

            if (m_state == State::Selecting)
            {
                // 框选过程中：浅蓝虚线框
                p.setPen(QPen(QColor(0, 150, 255), 1, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawRect(sel);
            }
            else
            {
                // 编辑态：绘制标注内容（选区局部坐标 → 画布坐标）
                p.save();
                // 裁剪到选区内：防止标注拖出选区时，未填充内部透出灰蒙版显示为黑色
                p.setClipRect(sel);
                p.translate(sel.topLeft());
                // 文字重编辑时，原标注项不绘制（内容由输入框替代显示，避免文字/边框双重重叠）
                if (m_textEditing && m_textEditItem >= 0 && m_textEditItem < m_canvas.items.size())
                {
                    QVector<AnnotationItem> items = m_canvas.items;
                    items.removeAt(m_textEditItem);
                    AnnotationPainter::draw(p, items);
                }
                else
                {
                    AnnotationPainter::draw(p, m_canvas.items);
                }
                // 正在绘制的草稿（工具策略渲染）
                if (m_drawing && m_tool)
                    m_tool->paintDraft(p);
                // 选择模式：被选中标注的边框 + 控制点（选区局部坐标）
                // （文字重编辑时原项不绘制选中框，避免与蓝色编辑虚线框重叠）
                if (m_selectedItem >= 0 && m_selectedItem < m_canvas.items.size() && !(m_textEditing && m_textEditItem == m_selectedItem))
                {
                    const AnnotationItem &selIt = m_canvas.items[m_selectedItem];
                    const QVector<Handle> itemHandles = AnnotationPainter::handlesFor(selIt);
                    const bool isSegment = (selIt.type == AnnotationType::Line || selIt.type == AnnotationType::Arrow);
                    // 矩形类：外接虚线框 + 8 控制点；线段类：仅显示起点/终点两个端点控制点
                    if (!isSegment)
                    {
                        const QRectF b = AnnotationPainter::itemBounds(selIt);
                        p.setPen(QPen(QColor(0, 200, 120), 1, Qt::DashLine));
                        p.setBrush(Qt::NoBrush);
                        p.drawRect(b);
                    }
                    p.setPen(QPen(QColor(0, 200, 120), 1));
                    p.setBrush(Qt::white);
                    for (Handle h : itemHandles)
                        p.drawRect(handleRect(selIt, h));
                }
                p.restore();

                // 蓝色 1.5pt 实线边框
                p.setPen(QPen(QColor(0, 120, 255), 1.5, Qt::SolidLine));
                p.setBrush(Qt::NoBrush);
                p.drawRect(sel);

                // 8 个控制点（白色方块 + 蓝色描边）
                const Handle handles[] = {
                    Handle::TL, Handle::T, Handle::TR,
                    Handle::R, Handle::BR, Handle::B,
                    Handle::BL, Handle::L};
                p.setBrush(Qt::white);
                p.setPen(QPen(QColor(0, 120, 255), 1));
                for (Handle h : handles)
                    p.drawRect(handleRect(h).translated(-m_cap.desktop.topLeft()));
            }

            // 尺寸提示（物理分辨率 = 最终输出尺寸）
            p.setPen(Qt::white);
            const QSize phys = ScreenCapturer::toPhysicalRect(m_selection).size();
            p.drawText(sel.translated(4, -16),
                       QString("%1 × %2").arg(phys.width()).arg(phys.height()));
        }

        // ESC 二次退出提示：半透明灰框 + 蓝字（第一次 ESC 时显示，2 秒后消退）
        if (m_escArmed)
        {
            const QRectF box(0, 0, 200, 52);
            const QPointF c = rect().center();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(60, 60, 60, 200)); // 半透明灰色
            p.drawRoundedRect(QRectF(c.x() - box.width() / 2, c.y() - box.height() / 2,
                                     box.width(), box.height()),
                              10, 10);
            QFont hintFont = p.font();
            hintFont.setPointSizeF(13);
            p.setFont(hintFont);
            p.setPen(QColor(0, 120, 255)); // 蓝色提示文字
            p.drawText(QRectF(c.x() - box.width() / 2, c.y() - box.height() / 2,
                              box.width(), box.height()),
                       Qt::AlignCenter, "再按 ESC 退出");
        }
    }

    // ==================== 控制点几何 ====================

    QRectF CaptureWindow::handleRect(const QRectF &r, Handle h) const
    {
        const QPointF c = r.center();
        QPointF pt;
        switch (h)
        {
        case Handle::TL:
            pt = r.topLeft();
            break;
        case Handle::T:
            pt = QPointF(c.x(), r.top());
            break;
        case Handle::TR:
            pt = r.topRight();
            break;
        case Handle::R:
            pt = QPointF(r.right(), c.y());
            break;
        case Handle::BR:
            pt = r.bottomRight();
            break;
        case Handle::B:
            pt = QPointF(c.x(), r.bottom());
            break;
        case Handle::BL:
            pt = r.bottomLeft();
            break;
        case Handle::L:
            pt = QPointF(r.left(), c.y());
            break;
        case Handle::None:
            return QRectF();
        case Handle::P0:
        case Handle::P1:
            return QRectF(); // 端点控制点仅通过标注重载定位
        }
        return QRectF(pt - QPointF(kHandle, kHandle), QSizeF(2 * kHandle, 2 * kHandle));
    }

    // 标注控制点方块（局部坐标）：线段类端点（P0/P1）经 handlePos 定位
    QRectF CaptureWindow::handleRect(const AnnotationItem &it, Handle h) const
    {
        if (h == Handle::None)
            return QRectF();
        const QPointF pt = AnnotationPainter::handlePos(it, h);
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
            Handle::R, Handle::BR, Handle::B,
            Handle::BL, Handle::L};
        for (Handle h : handles)
        {
            if (handleRect(b, h).adjusted(-2, -2, 2, 2).contains(pos))
                return h;
        }
        return Handle::None;
    }

    CaptureWindow::Handle CaptureWindow::handleAt(const QPoint &pos) const
    {
        return handleAt(QRectF(m_selection), QPointF(pos));
    }

    // 标注控制点命中（局部坐标）：按类型遍历（矩形类 8 点 / 线段类 2 端点）
    CaptureWindow::Handle CaptureWindow::handleAt(const AnnotationItem &it, const QPointF &pos) const
    {
        for (Handle h : AnnotationPainter::handlesFor(it))
        {
            if (handleRect(it, h).adjusted(-2, -2, 2, 2).contains(pos))
                return h;
        }
        return Handle::None;
    }

    void CaptureWindow::setCursorFor(Handle h)
    {
        switch (h)
        {
        case Handle::TL:
        case Handle::BR:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case Handle::TR:
        case Handle::BL:
            setCursor(Qt::SizeBDiagCursor);
            break;
        case Handle::T:
        case Handle::B:
            setCursor(Qt::SizeVerCursor);
            break;
        case Handle::L:
        case Handle::R:
            setCursor(Qt::SizeHorCursor);
            break;
        case Handle::P0:
        case Handle::P1:
            setCursor(Qt::ArrowCursor);
            break; // 端点自由拖动
        case Handle::None:
            setCursor(Qt::CrossCursor);
            break;
        }
    }

    QRectF CaptureWindow::resizeRectF(QRectF r, Handle h, const QPointF &pos, qreal minSize) const
    {
        switch (h)
        {
        case Handle::TL:
            r.setTopLeft(QPointF(qMin(pos.x(), r.right() - minSize),
                                 qMin(pos.y(), r.bottom() - minSize)));
            break;
        case Handle::T:
            r.setTop(qMin(pos.y(), r.bottom() - minSize));
            break;
        case Handle::TR:
            r.setTopRight(QPointF(qMax(pos.x(), r.left() + minSize),
                                  qMin(pos.y(), r.bottom() - minSize)));
            break;
        case Handle::R:
            r.setRight(qMax(pos.x(), r.left() + minSize));
            break;
        case Handle::BR:
            r.setBottomRight(QPointF(qMax(pos.x(), r.left() + minSize),
                                     qMax(pos.y(), r.top() + minSize)));
            break;
        case Handle::B:
            r.setBottom(qMax(pos.y(), r.top() + minSize));
            break;
        case Handle::BL:
            r.setBottomLeft(QPointF(qMin(pos.x(), r.right() - minSize),
                                    qMax(pos.y(), r.top() + minSize)));
            break;
        case Handle::L:
            r.setLeft(qMin(pos.x(), r.right() - minSize));
            break;
        case Handle::None:
            break;
        }
        return r;
    }

    void CaptureWindow::resizeSelection(Handle h, const QPoint &pos)
    {
        const QRectF n = resizeRectF(QRectF(m_handleStartSel), h, QPointF(pos), kMinSize);
        m_selection = n.toRect().intersected(m_cap.desktop);
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
        updateFontSizeDisplay(); // 无选中 → 显示默认字号
        update();
    }

    void CaptureWindow::positionToolbar()
    {
        m_toolbar->adjustSize();
        const QRect sel = m_selection.translated(-m_cap.desktop.topLeft());
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

    void CaptureWindow::onToolSelected(Tool t)
    {
        // 切换工具前先保存/删除待输入文字
        if (m_textEditing)
            commitTextEdit();
        m_canvas.tool = t;
        m_drawing = false;
        m_selectedItem = -1; // 切换工具取消标注选中
        m_itemMoving = false;
        m_itemActiveHandle = Handle::None;
        m_escArmed = false; // 切换工具清除 ESC 二次退出提示
        m_escTimer.stop();

        // 工厂创建具体工具（策略模式）；None=选择模式由窗口处理
        m_ctx.selection = m_selection;
        m_tool = ToolFactory::create(t, &m_ctx);
        setCursor(m_tool ? m_tool->cursor() : Qt::SizeAllCursor);
        updateFontSizeDisplay(); // 切换工具取消选中 → 显示默认字号
    }

    void CaptureWindow::onColorSelected(const QColor &c)
    {
        if (m_textEditing)
        {
            // 文字输入态：颜色只对"当前选中或接下来输入的内容"负责（与字号一致，不提交编辑框）。
            // 有选区 → 应用到选中文字；无选区 → 设为后续输入文字的颜色。
            // 同时更新 m_canvas.color（新建标注/下一文本框用新色）；
            // 当前框内已输入文字（无显式色片段）渲染时仍用 m_textEditStartColor 兜底，不受影响。
            m_textEdit->setTextColor(c);
            m_canvas.color = c;
            m_textEdit->setFocus(); // 焦点还给编辑框：用户可直接继续输入，不会误入工具栏控件
            update();
            return;
        }
        m_canvas.color = c;
    }

    // ==================== 输出合成（保存 / 复制 / 确认） ====================
    // 输出为物理分辨率：逻辑选区 → 物理矩形裁剪；标注为选区局部逻辑坐标，
    // 通过 scale(k,k) 等比放大（k = 物理宽 / 逻辑宽）。

    QPixmap CaptureWindow::renderResult() const
    {
        const QRect crop = ScreenCapturer::toPhysicalRect(m_selection)
                               .translated(-m_cap.physDesktop.topLeft());
        QPixmap result = m_cap.screen.copy(crop);
        {
            QPainter p(&result);
            const qreal k = qreal(result.width()) / qMax(1, m_selection.width());
            p.scale(k, k);
            AnnotationPainter::draw(p, m_canvas.items);
            if (m_drawing && m_tool)
                m_tool->paintDraft(p);
        }
        return result;
    }

    void CaptureWindow::onSave()
    {
        if (m_textEditing)
            commitTextEdit();
        const QPixmap result = renderResult();
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
        QGuiApplication::clipboard()->setPixmap(renderResult());
        exitCapture(); // 复制后退出截图界面（显式操作才退出，防误触）
    }

    // ==================== 画板内文字识别（OCR） ====================

    void CaptureWindow::onOcrRequested()
    {
        if (m_state != State::Editing || m_selection.isEmpty())
            return;
        if (m_textEditing)
            commitTextEdit();
        if (m_ocr.busy())
            return; // 上次识别未结束，忽略重复点击
        if (!m_ocr.available())
        {
            QMessageBox::warning(this, "文字识别",
                                 "未找到 OCR 引擎 tesseract.exe，请确认其位于程序目录的 tesseract 文件夹中。");
            return;
        }

        // 裁剪选区（物理分辨率）交给 OCR；结果文本框为裁剪图像素坐标
        const QRect physCrop = ScreenCapturer::toPhysicalRect(m_selection)
                                   .translated(-m_cap.physDesktop.topLeft());
        if (physCrop.isEmpty())
            return;
        m_ocrImg = m_cap.screen.copy(physCrop).toImage();
        m_ocr.recognizeBoxes(m_ocrImg);
    }

    void CaptureWindow::onOcrBoxes(bool ok, const QVector<OcrBox> &boxes, const QString &error)
    {
        if (!ok)
        {
            QMessageBox::warning(this, "文字识别",
                                 error.isEmpty() ? QStringLiteral("未识别到文字") : error);
            return;
        }

        // 裁剪图像素坐标 → 选区局部逻辑坐标（按各轴缩放比例换算）
        const int selW = qMax(1, m_selection.width());
        const int selH = qMax(1, m_selection.height());
        const qreal kx = qreal(m_ocrImg.width()) / selW;
        const qreal ky = qreal(m_ocrImg.height()) / selH;
        const QRectF localClip(0, 0, selW, selH);

        for (const OcrBox &box : boxes)
        {
            QRectF local(box.rect.x() / kx, box.rect.y() / ky,
                         box.rect.width() / kx, box.rect.height() / ky);
            local = local.intersected(localClip);
            if (local.isEmpty() || box.text.trimmed().isEmpty())
                continue;

            // 文本框覆盖原图：字号与原始文字大小匹配。
            // OCR 行框高度 ≈ 原文字实际绘制高度，用 QFontMetricsF 实测迭代，使覆盖文字
            // 高度落在 [local.height()-1, local.height()] 区间（pixelSize 与字体高度近似线性）。
            AnnotationItem it;
            it.type = AnnotationType::Text;
            it.color = m_canvas.color;
            it.width = m_canvas.width;
            it.rect = local;
            it.text = box.text;
            QFont f(QStringLiteral("Microsoft YaHei"));
            f.setPixelSize(qBound(6, qRound(local.height() * 0.8), 200));
            QFontMetricsF fm(f);
            if (!qFuzzyIsNull(fm.height()))
            {
                // 比例修正一次即可逼近目标（字体高度 ∝ pixelSize）
                const int ps = qBound(6, qRound(f.pixelSize() * local.height() / fm.height()), 200);
                f.setPixelSize(ps);
                fm = QFontMetricsF(f);
            }
            // 精调 ±1px：不超过原文字高度，也不明显小于它
            for (int guard = 0; guard < 20; ++guard)
            {
                if (fm.height() > local.height() && f.pixelSize() > 6)
                {
                    f.setPixelSize(f.pixelSize() - 1);
                    fm = QFontMetricsF(f);
                }
                else if (fm.height() + 1.0 < local.height() && f.pixelSize() < 200)
                {
                    f.setPixelSize(f.pixelSize() + 1);
                    fm = QFontMetricsF(f);
                }
                else
                {
                    break;
                }
            }
            it.font = f;
            m_canvas.addItem(it);
        }
        m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
        update();
    }

    // ==================== 标注工具 ====================

    QPointF CaptureWindow::toLocal(const QPoint &global) const
    {
        return QPointF(global - m_selection.topLeft());
    }

    void CaptureWindow::startDraw(const QPointF &pos)
    {
        if (!m_tool)
            return;
        m_drawing = true;
        m_tool->onPress(pos);
        // 橡皮 / 文字无草稿交互，立即结束绘制态
        if (m_canvas.tool == Tool::Eraser || m_canvas.tool == Tool::Text)
            m_drawing = false;
        update();
    }

    void CaptureWindow::continueDraw(const QPointF &pos)
    {
        if (!m_drawing || !m_tool)
            return;
        m_tool->onDrag(pos); // 工具内部经 requestUpdate 触发重绘
        update();
    }

    void CaptureWindow::finishDraw(const QPointF &pos)
    {
        if (!m_drawing || !m_tool)
            return;
        m_tool->onRelease(pos);
        m_drawing = false;
        update();
    }

    // 马赛克笔刷：沿笔刷足迹（折线 + 圆头）像素化，足迹以外区域保持透明
    AnnotationItem CaptureWindow::buildMosaicItem(const QPolygonF &points, qreal width) const
    {
        AnnotationItem draft;
        draft.type = AnnotationType::Mosaic;
        draft.width = width;
        if (points.isEmpty())
            return draft;

        const qreal r = qMax(2.0, width / 2.0); // 笔刷半径
        // 足迹外接矩形（选区局部坐标），限制在选区内
        QRectF bounds;
        for (const QPointF &pt : points)
            bounds = bounds.united(QRectF(pt - QPointF(r, r), QSizeF(2 * r, 2 * r)));
        const QRect local = bounds.toAlignedRect()
                                .adjusted(-1, -1, 1, 1)
                                .intersected(QRect(QPoint(0, 0), m_selection.size()));
        if (local.isEmpty())
            return draft;

        // 截取屏幕源区域并整体像素化（m_cap.screen 为物理像素 → 换算物理矩形）
        const QRect global = local.translated(m_selection.topLeft()).intersected(m_cap.desktop);
        if (global.isEmpty())
            return draft;
        const QRect physGlobal(QRect(ScreenCapturer::toPhysical(global.topLeft()),
                                     ScreenCapturer::toPhysical(global.bottomRight() + QPoint(1, 1)) - QPoint(1, 1)));
        if (physGlobal.isEmpty())
            return draft;
        const QPixmap region = m_cap.screen.copy(physGlobal.translated(-m_cap.physDesktop.topLeft()));
        const int block = 8; // 马赛克块大小（物理像素）
        if (region.width() < block || region.height() < block)
            return draft;
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
            p.setPen(QPen(Qt::white, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::white);
            const QPointF off = QPointF(local.topLeft()); // 选区局部 → 区域局部
            QPolygonF pts;
            for (const QPointF &pt : points)
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

        draft.mosaicImage = mosaic;
        draft.rect = QRectF(local);
        return draft;
    }

    // ==================== 文字内嵌输入框（PPT 风格） ====================

    void CaptureWindow::beginTextEdit(const QPointF &local, int itemIndex)
    {
        // 停止其它交互
        m_itemMoving = false;
        m_itemActiveHandle = Handle::None;

        m_textEditItem = itemIndex;
        m_textEditPos = local;

        // 新建文本默认字号 = 字号栏当前值（无任何文本选中时显示的"接下来输入文字的字号"）
        QFont f("Microsoft YaHei");
        f.setBold(true);
        f.setPointSizeF(m_defaultFontSize);
        QPointF boxPos = local;
        QString initialText; // 重编辑时的原文本（先设字体再填文本，保证文档字符格式用目标字体）
        QString initialHtml; // 重编辑时的富文本（还原多字号）
        if (itemIndex >= 0)
        {
            const AnnotationItem &it = m_canvas.items[itemIndex];
            if (it.type != AnnotationType::Text)
                return;
            f = it.font;
            // OCR 等以像素字号创建的文本 → 换算为 pt（96 DPI），保证字号栏与富文本一致
            if (f.pointSizeF() <= 0 && f.pixelSize() > 0)
                f.setPointSizeF(f.pixelSize() * 72.0 / 96.0);
            initialText = it.text;
            initialHtml = it.html;
            boxPos = it.rect.topLeft();      // 待输入框贴在原文字位置
            m_selectedItem = itemIndex;      // 重编辑的文本保持选中态
            m_textEditStartColor = it.color; // 兜底色 = 原标注色（输入期间改色不影响已输入文字）
        }
        else
        {
            m_selectedItem = -1;
            m_textEditStartColor = m_canvas.color; // 兜底色 = 本次输入开始时的标注色
        }
        m_textEdit->setFont(f);
        if (itemIndex >= 0)
        {
            if (initialHtml.isEmpty())
                m_textEdit->setPlainText(initialText);
            else
                m_textEdit->setHtml(initialHtml); // 还原多字号片段
        }
        else
        {
            m_textEdit->clear();
        }

        // 输入中不显示白色底：背景透明，仅虚线标出输入区域；默认文本色 = 本次输入开始色
        // （与提交后渲染兜底色 it.color 一致：无显式色的文字编辑中/提交后显示同色）
        m_textEdit->setStyleSheet(QString(
                                      "QTextEdit { background: transparent; border: 1px dashed #0078ff;"
                                      " border-radius: 3px; padding: 1px 2px; color: %1; }")
                                      .arg(m_textEditStartColor.name()));

        // 定位：选区局部坐标 → 全局坐标 → 窗口坐标
        const QPointF global = boxPos + m_selection.topLeft();
        const QPoint win = (global - QPointF(m_cap.desktop.topLeft())).toPoint();
        m_textEdit->move(win);
        m_textEdit->show();
        m_textEdit->raise();
        m_textEdit->setFocus();
        m_textEdit->selectAll();
        m_textEditing = true;
        adjustTextEditSize();    // 初始尺寸：宽 240 × 单行高；一旦输入即随内容自适应
        updateFontSizeDisplay(); // 输入框已全选 → 字号栏反映其字号（多字号则置空）
        update();
    }

    // 输入框尺寸自适应：宽 = 最长行的宽度，高 = 全部行的高度；
    // 未输入内容时保持初始长宽（240 × 单行高），一旦输入即实时贴合内容（长宽由内容决定，不可手动调整）
    void CaptureWindow::adjustTextEditSize()
    {
        if (!m_textEditing || !m_textEdit->isVisible())
            return;
        const QSizeF doc = m_textEdit->document()->size(); // 无换行模式：即文本自然尺寸
        const int lineH = qMax(1, m_textEdit->fontMetrics().height());
        const bool empty = m_textEdit->toPlainText().isEmpty();
        const int w = empty ? 240 : qMax(8, qCeil(doc.width()) + 6);    // + 边框(1px×2) + 水平内边距(2px×2)
        const int h = empty ? lineH : qMax(8, qCeil(doc.height()) + 4); // + 边框(1px×2) + 垂直内边距(1px×2)
        m_textEdit->resize(w, h);
    }

    void CaptureWindow::commitTextEdit()
    {
        if (!m_textEditing)
            return;
        const QString text = m_textEdit->toPlainText().trimmed();
        const QString html = m_textEdit->toHtml();                   // 富文本（含多字号片段）——须在 clear 前记录
        const QSizeF content = m_textEdit->document()->size();       // 提交前记录：宽=最长行，高=总行高
        const QFont docFont = m_textEdit->document()->defaultFont(); // 文档默认字体（family/bold/默认字号）
        const int itemIdx = m_textEditItem;
        const QPointF localPos = m_textEditPos;
        m_textEdit->hide();
        m_textEdit->clear();
        m_textEditing = false;
        m_textEditItem = -1;

        // 重编辑期间原标注可能已被撤销/删除 → 直接丢弃本次编辑
        if (itemIdx >= 0 && itemIdx >= m_canvas.items.size())
        {
            updateFontSizeDisplay();
            update();
            return;
        }

        if (text.isEmpty())
        {
            // 无输入：删除文本框（新建则丢弃；重编辑则删除该标注）
            if (itemIdx >= 0)
            {
                m_canvas.pushSnapshot();
                m_canvas.removeItem(itemIdx);
                m_selectedItem = -1;
            }
            updateFontSizeDisplay();
            update();
            return;
        }

        // 文本框尺寸 = 内容自然尺寸（宽=最长行的宽度，高=全部行的高度）
        const QSizeF box(qMax(content.width(), 8.0), qMax(content.height(), 8.0));
        if (itemIdx >= 0)
        {
            // 重编辑：更新文字内容 / 富文本（含多字号）与框尺寸（颜色保持原有）
            AnnotationItem it = m_canvas.items[itemIdx];
            m_canvas.pushSnapshot();
            it.text = text;
            it.html = html;
            it.font = docFont;
            it.rect = QRectF(it.rect.topLeft(), box);
            m_canvas.setItem(itemIdx, it);
        }
        else
    {
        // 新建文字标注：兜底色 = 输入开始时的标注色（输入期间改过颜色不影响已输入文字）
        AnnotationItem it;
        it.type = AnnotationType::Text;
        it.color = m_textEditStartColor;
        it.font = docFont;
        it.text = text;
        it.html = html;
        it.rect = QRectF(localPos, box);
        m_canvas.addItem(it);
    }
        m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
        updateFontSizeDisplay();
        update();
    }

    // ==================== 字号栏（文字大小，pt） ====================
    // 逻辑：字号栏永远体现"当前所选中，或所编辑的文本的大小"——
    //   - 正在输入：有选区 → 扫描选区片段字号（多字号置空 / 统一显示该值）
    //   - 正在输入：无选区 → 显示光标处当前格式（即接下来输入文字的字号）
    //   - 选中文字标注（非输入态）：整条扫描（多字号置空 / 统一显示该值）
    //   - 其余情况：显示默认字号（接下来输入文字的字号）

    qreal CaptureWindow::fragmentPt(const QTextCharFormat &fmt, const QFont &def) const
    {
        qreal pt = fmt.fontPointSize();
        if (pt > 0)
            return pt;
        // 片段未显式设置字号（继承文档默认）→ 用默认字体兜底
        pt = def.pointSizeF();
        if (pt > 0)
            return pt;
        // 像素字号 → 换算 pt（96 DPI）
        if (def.pixelSize() > 0)
            return def.pixelSize() * 72.0 / 96.0;
        return 14.0;
    }

    void CaptureWindow::updateFontSizeDisplay()
    {
        if (!m_toolbar)
            return;

        if (m_textEditing)
        {
            const QTextCursor cur = m_textEdit->textCursor();
            const QFont def = m_textEdit->document()->defaultFont();
            if (cur.hasSelection())
            {
                // 有选区：扫描选中范围内片段的字号
                const int selStart = cur.selectionStart();
                const int selEnd = cur.selectionEnd();
                qreal size = -1;
                bool mixed = false;
                QTextBlock b = m_textEdit->document()->begin();
                while (b.isValid() && !mixed)
                {
                    for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it)
                    {
                        const int fEnd = it.fragment().position() + it.fragment().length();
                        if (qMax(it.fragment().position(), selStart) >= qMin(fEnd, selEnd))
                            continue; // 片段与选区无交集
                        const qreal pt = fragmentPt(it.fragment().charFormat(), def);
                        if (size < 0)
                            size = pt;
                        else if (!qFuzzyCompare(size, pt))
                        {
                            mixed = true;
                            break;
                        }
                    }
                    b = b.next();
                }
                if (mixed || size < 0)
                    m_toolbar->clearFontSize(); // 多字号 → 不显示
                else
                    m_toolbar->setFontSize(size);
            }
            else
            {
                // 无选区：显示接下来输入文字的字号（光标处当前格式）
                m_toolbar->setFontSize(fragmentPt(m_textEdit->currentCharFormat(), def));
            }
            return;
        }

        if (m_selectedItem >= 0 && m_selectedItem < m_canvas.items.size() && m_canvas.items[m_selectedItem].type == AnnotationType::Text)
        {
            // 选中文字标注（非输入态）：扫描整条的字号
            const AnnotationItem &it = m_canvas.items[m_selectedItem];
            qreal size = -1;
            bool mixed = false;
            if (it.html.isEmpty())
            {
                size = it.font.pointSizeF();
                if (size <= 0 && it.font.pixelSize() > 0)
                    size = it.font.pixelSize() * 72.0 / 96.0; // OCR 像素字号 → pt
            }
            else
            {
                QTextDocument doc;
                doc.setDefaultFont(it.font);
                doc.setHtml(it.html);
                const QFont def = doc.defaultFont();
                QTextBlock b = doc.begin();
                while (b.isValid() && !mixed)
                {
                    for (QTextBlock::iterator frag = b.begin(); !frag.atEnd(); ++frag)
                    {
                        const qreal pt = fragmentPt(frag.fragment().charFormat(), def);
                        if (size < 0)
                            size = pt;
                        else if (!qFuzzyCompare(size, pt))
                        {
                            mixed = true;
                            break;
                        }
                    }
                    b = b.next();
                }
            }
            if (mixed || size < 0)
                m_toolbar->clearFontSize();
            else
                m_toolbar->setFontSize(size);
            return;
        }

        // 无任何文本选中：默认字号 = 接下来输入文字的字号
        m_toolbar->setFontSize(m_defaultFontSize);
    }

    void CaptureWindow::onFontSizeSelected(qreal pt)
    {
        if (m_textEditing)
        {
            // 正在输入：有选中文字 → 立即改选中文字大小；无选中 → 设为后续输入字号
            m_textEdit->setFontPointSize(pt);
            // 无选中时同步"接下来输入文字的字号"（新建文本框 beginTextEdit 用此值，
            // 否则本框后续输入用了新字号，但提交后新建文本框仍沿用旧字号）
            if (!m_textEdit->textCursor().hasSelection())
                m_defaultFontSize = pt;
            m_textEdit->setFocus(); // 焦点还给编辑框：改完字号可直接继续输入
            adjustTextEditSize(); // 字号变化 → 文本框尺寸跟随
            updateFontSizeDisplay();
            update();
            return;
        }
        if (m_selectedItem >= 0 && m_selectedItem < m_canvas.items.size() && m_canvas.items[m_selectedItem].type == AnnotationType::Text)
        {
            // 选中文字标注（非输入态）：整条文本改为统一字号
            AnnotationItem it = m_canvas.items[m_selectedItem];
            m_canvas.pushSnapshot();
            applyFontSizeToItem(it, pt);
            m_canvas.setItem(m_selectedItem, it);
            m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
            updateFontSizeDisplay();
            update();
            return;
        }
        // 无任何文本选中：作为接下来输入文字的字号
        m_defaultFontSize = pt;
    }

    void CaptureWindow::applyFontSizeToItem(AnnotationItem &it, qreal pt)
    {
        QTextDocument doc;
        doc.setDocumentMargin(0);
        if (it.html.isEmpty())
        {
            doc.setDefaultFont(it.font);
            doc.setPlainText(it.text);
        }
        else
        {
            doc.setDefaultFont(it.font);
            doc.setHtml(it.html);
        }
        {
            QTextCursor cur(&doc);
            cur.select(QTextCursor::Document);
            QTextCharFormat fmt;
            fmt.setFontPointSize(pt);
            cur.mergeCharFormat(fmt);
        }
        // 文档默认字号同步为新值：body 的 font-size 保持一致，重新编辑时后续输入用新字号
        QFont def = doc.defaultFont();
        def.setPointSizeF(pt);
        doc.setDefaultFont(def);
        it.font = def;
        it.html = doc.toHtml();
        it.text = doc.toPlainText();
        // 重算框尺寸 = 内容自然尺寸（不换行：宽=最长行，高=总行高）
        const QSizeF box = doc.size();
        it.rect = QRectF(it.rect.topLeft(),
                         QSizeF(qMax(box.width(), 8.0), qMax(box.height(), 8.0)));
    }

    // ==================== 选择模式：标注项拖动 / 缩放 ====================

    void CaptureWindow::moveSelectedItem(const QPointF &local)
    {
        if (m_selectedItem < 0 || m_selectedItem >= m_canvas.items.size())
            return;
        const QPointF delta = local - m_itemStartPos;
        if (!m_itemSnapshotted)
        {
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
        if (m_itemActiveHandle == Handle::None)
            return;
        // 按控制点类型编辑：矩形类镜像缩放（拖过对侧起始边翻转），线段类端点编辑
        const AnnotationItem it = AnnotationPainter::resizedItem(m_itemStartGeom,
                                                                 m_itemActiveHandle, local, kMinSize);
        if (!m_itemSnapshotted)
        {
            m_canvas.pushSnapshot();
            m_itemSnapshotted = true;
        }
        m_canvas.setItem(m_selectedItem, it);
        update();
    }

    // ==================== 鼠标事件 ====================

    void CaptureWindow::mousePressEvent(QMouseEvent *event)
    {
        const QPoint pos = event->globalPosition().toPoint();

        if (event->button() == Qt::RightButton)
        {
            cancel();
            return;
        }

        if (event->button() != Qt::LeftButton)
            return;

        if (m_state == State::Selecting)
        {
            m_origin = pos;
            m_selection = QRect(m_origin, QSize(0, 0));
            update();
            return;
        }

        if (m_state == State::Editing)
        {
            // 正在输入文字时，点击其它位置先提交（空删 / 非空存），再处理本次点击
            if (m_textEditing)
                commitTextEdit();

            // 选择模式（None）且点在选区内：标注项选中 / 拖动 / 缩放，或拖动选区
            if (m_canvas.tool == Tool::None && m_selection.contains(pos))
            {
                const QPointF local = toLocal(pos);
                // 0) 选区自身的控制点优先（与标注项控制点互斥）
                const Handle selHandle = handleAt(pos);
                if (selHandle != Handle::None)
                {
                    m_activeHandle = selHandle;
                    m_handleStartPos = pos;
                    m_handleStartSel = m_selection;
                    m_selectedItem = -1;
                    updateFontSizeDisplay();
                    setCursorFor(selHandle);
                    return;
                }
                // 1) 已有选中项：点其控制点 → 开始缩放
                if (m_selectedItem >= 0 && m_selectedItem < m_canvas.items.size())
                {
                    const AnnotationItem &sel = m_canvas.items[m_selectedItem];
                    const Handle h = handleAt(sel, local);
                    if (h != Handle::None)
                    {
                        m_itemActiveHandle = h;
                        m_itemStartPos = local;
                        m_itemStartBounds = AnnotationPainter::itemBounds(sel);
                        m_itemStartGeom = sel;
                        m_itemSnapshotted = false;
                        setCursorFor(h);
                        return;
                    }
                    m_selectedItem = -1; // 不在选中项控制点上 → 取消选中（下续可能点选新标注）
                    updateFontSizeDisplay();
                }
                // 2) 点选标注项 → 选中并准备拖动
                const int idx = m_canvas.itemAt(local);
                if (idx >= 0)
                {
                    m_selectedItem = idx;
                    const AnnotationItem &sel = m_canvas.items[idx];
                    m_itemMoving = true;
                    m_itemStartPos = local;
                    m_itemStartBounds = AnnotationPainter::itemBounds(sel);
                    m_itemStartGeom = sel;
                    m_itemSnapshotted = false;
                    setCursor(Qt::SizeAllCursor);
                    updateFontSizeDisplay();
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
            if (m_canvas.tool != Tool::None && m_selection.contains(pos))
            {
                startDraw(toLocal(pos));
                return;
            }

            // 选区外：控制点 / 移动（单击不再重画，防误触；重画由双击选区外触发）
            m_selectedItem = -1;
            updateFontSizeDisplay();
            m_activeHandle = handleAt(pos);
            if (m_activeHandle != Handle::None)
            {
                m_handleStartPos = pos;
                m_handleStartSel = m_selection;
                setCursorFor(m_activeHandle);
                return;
            }
            if (m_selection.contains(pos))
            {
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

        if (m_state == State::Selecting && event->buttons() & Qt::LeftButton)
        {
            m_selection = QRect(m_origin, pos).normalized();
            update();
            return;
        }

        if (m_state == State::Editing)
        {
            // 正在绘制标注
            if (m_drawing && event->buttons() & Qt::LeftButton)
            {
                continueDraw(toLocal(pos));
                return;
            }
            // 拖动选中标注的控制点（缩放）
            if (m_itemActiveHandle != Handle::None && event->buttons() & Qt::LeftButton)
            {
                resizeSelectedItem(toLocal(pos));
                return;
            }
            // 拖动选中标注（移动）
            if (m_itemMoving && event->buttons() & Qt::LeftButton)
            {
                moveSelectedItem(toLocal(pos));
                return;
            }
            // 拖动选区控制点
            if (m_activeHandle != Handle::None && event->buttons() & Qt::LeftButton)
            {
                resizeSelection(m_activeHandle, pos);
                return;
            }
            // 整体移动选区（标注为局部坐标，随选区平移）
            if (m_moving && event->buttons() & Qt::LeftButton)
            {
                QRect r = m_selection;
                r.moveTopLeft(pos - m_moveOffset);
                r = r.intersected(m_cap.desktop);
                if (r.width() >= kMinSize && r.height() >= kMinSize)
                {
                    m_selection = r;
                    positionToolbar();
                    update();
                }
                return;
            }
            // 未按下：光标反馈
            if (m_canvas.tool == Tool::None)
            {
                const QPointF local = toLocal(pos);
                const bool overSelHandle = handleAt(pos) != Handle::None;
                if (!m_selection.contains(pos) || overSelHandle)
                {
                    setCursorFor(handleAt(pos));
                }
                else if (m_selectedItem >= 0 && m_selectedItem < m_canvas.items.size())
                {
                    // 选中标注：控制点光标 / 项内移动光标
                    const AnnotationItem &sel = m_canvas.items[m_selectedItem];
                    const Handle h = handleAt(sel, local);
                    if (h != Handle::None)
                        setCursorFor(h);
                    else
                        setCursor(AnnotationPainter::itemBounds(sel).adjusted(-4, -4, 4, 4).contains(local)
                                      ? Qt::SizeAllCursor
                                      : Qt::ArrowCursor);
                }
                else
                {
                    setCursor(m_canvas.hitTest(local) ? Qt::ArrowCursor : Qt::SizeAllCursor);
                }
            }
            else if (m_selection.contains(pos))
            {
                setCursor(m_canvas.tool == Tool::Eraser
                              ? Qt::PointingHandCursor
                              : Qt::CrossCursor);
            }
            else
            {
                setCursorFor(handleAt(pos));
            }
        }
    }

    void CaptureWindow::mouseReleaseEvent(QMouseEvent *event)
    {
        if (event->button() != Qt::LeftButton)
            return;

        if (m_state == State::Selecting)
        {
            m_selection = QRect(m_origin, event->globalPosition().toPoint()).normalized();
            // 过小视为取消
            if (m_selection.width() < kMinSize || m_selection.height() < kMinSize)
            {
                cancel();
                return;
            }
            enterEditingState();
            return;
        }

        if (m_state == State::Editing)
        {
            // 结束绘制
            if (m_drawing)
            {
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
        if (m_selection.contains(pos))
        {
            // 选择模式：双击文字标注 → 重新编辑
            if (m_canvas.tool == Tool::None)
            {
                const QPointF local = toLocal(pos);
                const int idx = m_canvas.itemAt(local);
                if (idx >= 0 && m_canvas.items[idx].type == AnnotationType::Text)
                {
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
        if (event->key() == Qt::Key_Escape)
        {
            if (m_textEditing)
            {
                m_textEdit->clear(); // 空文本提交 → 删除待输入框 / 标注
                commitTextEdit();
                return;
            }
            // 框选过程等非编辑态：直接退出（尚未产生可编辑内容）
            if (m_state != State::Editing)
            {
                cancel();
                return;
            }
            // 工具模式：ESC 退出到选择模式（放弃当前草稿），不触发退出提示
            if (m_canvas.tool != Tool::None)
            {
                m_escArmed = false;
                m_toolbar->setTool(Tool::None);
                onToolSelected(Tool::None);
                return;
            }
            // 选择模式：连按两次 ESC 才退出（防误触）；第一次显示 2 秒提示
            if (!m_escArmed)
            {
                m_escArmed = true;
                m_escTimer.start(2000); // 2 秒后提示消退，需要重新按两次
                update();
            }
            else
            {
                m_escTimer.stop();
                cancel();
            }
        }
        else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        {
            // 文字输入框内 Enter/Shift+Enter 由 QTextEdit 处理为换行（输入结束靠点击框外）
            if (m_state == State::Editing && !m_textEditing)
                confirm();
        }
        else if (event->key() == Qt::Key_Delete)
        {
            // 选择模式：删除选中的标注（输入框内 DEL 由 QTextEdit 自己处理，不介入）
            if (m_state == State::Editing && !m_textEditing && m_selectedItem >= 0 && m_selectedItem < m_canvas.items.size())
            {
                m_canvas.pushSnapshot(); // 压撤销栈，撤销可恢复被删项
                m_canvas.removeItem(m_selectedItem);
                m_selectedItem = -1;
                m_itemMoving = false;
                m_itemActiveHandle = Handle::None;
                m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
                updateFontSizeDisplay();
                update();
            }
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
        QPixmap result = renderResult();
        if (m_drawing)
            m_drawing = false;
        exitCapture();
        emit captured(result);
    }

    void CaptureWindow::cancel()
    {
        exitCapture();
        emit captured(QPixmap());
    }

} // namespace snipaste
