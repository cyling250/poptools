#include "CaptureWindow.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QGuiApplication>
#include <QClipboard>

#include "core/annotation/AnnotationPainter.h"

#include "ToolBar.h"
#include "CaptureContext.h"
#include "SelectionController.h"
#include "AnnotationEditor.h"
#include "ToolDrawingController.h"
#include "TextEditingController.h"
#include "OcrController.h"
#include "OutputController.h"

namespace snipaste
{

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

        // ---- 装配共享上下文（各控制器经此注入共享状态与窗口回调） ----
        m_ctx.cap = &m_cap;
        m_ctx.canvas = &m_canvas;
        m_ctx.toolbar = m_toolbar;
        m_ctx.selection = &m_selection;
        m_ctx.selectedItem = &m_selectedItem;
        m_ctx.requestUpdate = [this]
        { update(); };
        m_ctx.stateChanged = [this]
        {
            m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
            update();
        };
        m_ctx.selectionChanged = [this]
        {
            positionToolbar();   // 选区移动/选中项变化 → 工具栏跟随
            m_textEditCtrl->syncToolbar();
            update();
        };
        m_ctx.exitRequested = [this]
        { exitCapture(); };
        m_ctx.beginTextEdit = [this](const QPointF &local, int existing)
        {
            resetInteraction();
            m_textEditCtrl->begin(local, existing);
        };
        m_ctx.hasDraft = [this]
        { return m_toolDrawing->hasDraft(); };
        m_ctx.paintDraft = [this](QPainter &p)
        { m_toolDrawing->paintDraft(p); };

        // ---- 注册功能控制器（组合模式） ----
        m_selectionCtrl = std::make_unique<SelectionController>(&m_ctx);
        m_annotationEditor = std::make_unique<AnnotationEditor>(&m_ctx);
        m_toolDrawing = std::make_unique<ToolDrawingController>(&m_ctx);
        m_textEditCtrl = std::make_unique<TextEditingController>(&m_ctx);
        m_ocrCtrl = std::make_unique<OcrController>(&m_ctx);
        m_outputCtrl = std::make_unique<OutputController>(&m_ctx);
        m_textEditCtrl->init(this); // 创建文字内嵌输入框

        // ---- 工具栏信号（路由到控制器） ----
        connect(m_toolbar, &ToolBar::toolSelected, this, &CaptureWindow::onToolSelected);
        connect(m_toolbar, &ToolBar::colorSelected, this, &CaptureWindow::onColorSelected);
        connect(m_toolbar, &ToolBar::widthSelected, this, [this](qreal w)
                {
                    m_textEditCtrl->commit(); // 点击框外（工具栏）结束输入
                    m_canvas.width = w;
                });
        connect(m_toolbar, &ToolBar::filledToggled, this, [this](bool f)
                {
                    m_textEditCtrl->commit();
                    m_canvas.filled = f;
                });
        connect(m_toolbar, &ToolBar::fontSizeSelected, this, &CaptureWindow::onFontSizeSelected);
        connect(m_toolbar, &ToolBar::undoRequested, this, [this]
                {
                    m_textEditCtrl->commit();
                    m_canvas.undo();
                    m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
                    update();
                });
        connect(m_toolbar, &ToolBar::redoRequested, this, [this]
                {
                    m_textEditCtrl->commit();
                    m_canvas.redo();
                    m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
                    update();
                });
        connect(m_toolbar, &ToolBar::saveRequested, this, [this]
                {
                    m_textEditCtrl->commit();
                    m_outputCtrl->save(this);
                });
        connect(m_toolbar, &ToolBar::copyRequested, this, [this]
                {
                    m_textEditCtrl->commit();
                    m_outputCtrl->copy();
                });
        connect(m_toolbar, &ToolBar::ocrRequested, this, &CaptureWindow::onOcrRequested);

        // ESC 二次退出提示：2 秒后消退（重新按两次才会退出）
        m_escTimer.setSingleShot(true);
        connect(&m_escTimer, &QTimer::timeout, this, [this]
                {
                    m_escArmed = false;
                    update();
                });
    }

    // ==================== 截图生命周期 ====================

    void CaptureWindow::startCapture()
    {
        // 捕获所有屏幕拼接的物理分辨率画面（多屏扩展无黑屏），
        // 并记录逻辑/物理虚拟桌面几何（窗口/鼠标/选区均用逻辑坐标）
        m_cap = ScreenCapturer::capture();

        m_state = State::Selecting;
        m_selection = QRect();
        m_canvas.clear();
        m_canvas.tool = Tool::None; // 默认选择模式
        m_toolDrawing->setTool(Tool::None); // 清空上一轮工具实例
        m_selectionCtrl->cancelActive();
        m_annotationEditor->clearSelection();
        m_escArmed = false; // 重置 ESC 二次退出提示
        m_escTimer.stop();
        m_toolbar->hide();
        setGeometry(m_cap.desktop);
        show();
        activateWindow();
        raise();
        setFocus();
    }

    void CaptureWindow::exitCapture()
    {
        m_state = State::Idle;
        m_selection = QRect();
        m_toolbar->hide();
        m_selectionCtrl->cancelActive();
        m_annotationEditor->clearSelection();
        m_textEditCtrl->detach(); // 文字编辑态清理（隐藏输入框）
        hide();
    }

    void CaptureWindow::enterEditingState()
    {
        m_state = State::Editing;
        m_selectionCtrl->cancelActive();
        m_annotationEditor->clearSelection(); // 取消标注选中 + 字号栏刷新
        if (m_textEditCtrl->isEditing())
            m_textEditCtrl->commit();

        // 显示工具栏（贴画布下方靠右）
        positionToolbar();
        m_toolbar->show();
        m_toolbar->raise();
        m_toolbar->setUndoRedoEnabled(false, false);
        m_toolbar->setTool(m_canvas.tool);
        m_textEditCtrl->syncToolbar(); // 无选中 → 显示默认字号
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

    void CaptureWindow::resetInteraction()
    {
        m_selectionCtrl->cancelActive();
        m_annotationEditor->stopInteraction();
    }

    // ==================== 工具栏槽（路由到控制器） ====================

    void CaptureWindow::onToolSelected(Tool t)
    {
        if (m_textEditCtrl->isEditing())
            m_textEditCtrl->commit();
        m_canvas.tool = t;
        m_toolDrawing->setTool(t); // 工厂创建工具实例（None=选择模式）
        m_annotationEditor->clearSelection(); // 切换工具取消标注选中 + 字号栏刷新
        m_escArmed = false; // 切换工具清除 ESC 二次退出提示
        m_escTimer.stop();
        setCursor(m_toolDrawing->cursor());
    }

    void CaptureWindow::onColorSelected(const QColor &c)
    {
        if (m_textEditCtrl->isEditing())
        {
            m_textEditCtrl->applyColor(c); // 输入态：颜色只作用于当前选中/后续输入
            return;
        }
        m_canvas.color = c;
    }

    void CaptureWindow::onFontSizeSelected(qreal pt)
    {
        m_textEditCtrl->onFontSizeSelected(pt); // 三态：输入态 / 选中文字标注 / 无选中
    }

    void CaptureWindow::onOcrRequested()
    {
        if (m_state != State::Editing || m_selection.isEmpty())
            return;
        if (m_textEditCtrl->isEditing())
            m_textEditCtrl->commit();
        m_ocrCtrl->request(this);
    }

    // ==================== 绘制编排 ====================

    void CaptureWindow::paintEvent(QPaintEvent *event)
    {
        Q_UNUSED(event)
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform); // 物理原图缩放到逻辑窗口时保持平滑
        p.drawPixmap(rect(), m_cap.screen);               // 底层为原始屏幕画面（物理分辨率）

        if (m_state == State::Idle)
            return;

        // 灰色蒙版 + 框选虚线框（Selecting 态）
        m_selectionCtrl->paintMaskAndFrame(p, rect(), m_state == State::Selecting);

        if (m_state == State::Editing && !m_selection.isEmpty())
        {
            const QRect sel = m_selection.translated(-m_cap.desktop.topLeft());

            // 编辑态：绘制标注内容（选区局部坐标 → 画布坐标）
            p.save();
            // 裁剪到选区内：防止标注拖出选区时，未填充内部透出灰蒙版显示为黑色
            p.setClipRect(sel);
            p.translate(sel.topLeft());
            // 文字重编辑时，原标注项不绘制（内容由输入框替代显示，避免文字/边框双重重叠）
            const int editItem = m_textEditCtrl->editingItem();
            if (m_textEditCtrl->isEditing() && editItem >= 0 && editItem < m_canvas.items.size())
            {
                QVector<AnnotationItem> items = m_canvas.items;
                items.removeAt(editItem);
                AnnotationPainter::draw(p, items);
            }
            else
            {
                AnnotationPainter::draw(p, m_canvas.items);
            }
            // 正在绘制的草稿（工具策略渲染）
            m_toolDrawing->paintDraft(p);
            // 选中标注的边框 + 控制点（文字重编辑时不画原项选中框，避免与蓝色编辑虚线框重叠）
            if (!(m_textEditCtrl->isEditing() && editItem == m_selectedItem))
                m_annotationEditor->paintSelected(p);
            p.restore();

            // 蓝色 1.5pt 实线边框 + 控制点 + 尺寸提示
            m_selectionCtrl->paintSelectionFrame(p, sel);
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

    // ==================== 事件路由 ====================

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
            m_selectionCtrl->beginSelect(pos);
            return;
        }

        if (m_state == State::Editing)
        {
            // 正在输入文字时，点击其它位置先提交（空删 / 非空存），再处理本次点击
            if (m_textEditCtrl->isEditing())
                m_textEditCtrl->commit();

            // 选择模式（None）且点在选区内：标注项选中 / 拖动 / 缩放，或拖动选区
            if (m_canvas.tool == Tool::None && m_selection.contains(pos))
            {
                const QPointF local = toLocal(pos);
                // 0) 选区自身的控制点优先（与标注项控制点互斥）
                if (m_selectionCtrl->tryHandlePress(pos))
                    return;
                // 1) 2) 标注项：控制点缩放 / 点选拖动
                if (m_annotationEditor->tryPress(local))
                    return;
                // 3) 无标注 → 拖动整个选区
                m_selectionCtrl->tryStartMove(pos);
                return;
            }
            // 工具模式下：选区内开始绘制
            if (m_canvas.tool != Tool::None && m_selection.contains(pos))
            {
                m_toolDrawing->start(toLocal(pos));
                return;
            }

            // 选区外：控制点 / 移动（单击不再重画，防误触；重画由双击选区外触发）
            m_annotationEditor->clearSelection();
            if (m_selectionCtrl->tryHandlePress(pos))
                return;
            if (m_selection.contains(pos))
            {
                m_selectionCtrl->tryStartMove(pos);
                return;
            }
            // 选区外单击：忽略（防止误触退出编辑态）
        }
    }

    void CaptureWindow::mouseMoveEvent(QMouseEvent *event)
    {
        const QPoint pos = event->globalPosition().toPoint();

        if (m_state == State::Selecting && event->buttons() & Qt::LeftButton)
        {
            m_selectionCtrl->dragSelect(pos);
            return;
        }

        if (m_state == State::Editing)
        {
            // 正在绘制标注
            if (m_toolDrawing->isDrawing() && event->buttons() & Qt::LeftButton)
            {
                m_toolDrawing->drag(toLocal(pos));
                return;
            }
            // 拖动选中标注的控制点（缩放）
            if (m_annotationEditor->isResizing() && event->buttons() & Qt::LeftButton)
            {
                m_annotationEditor->drag(toLocal(pos));
                return;
            }
            // 拖动选中标注（移动）
            if (m_annotationEditor->isMoving() && event->buttons() & Qt::LeftButton)
            {
                m_annotationEditor->drag(toLocal(pos));
                return;
            }
            // 拖动选区控制点 / 整体移动
            if (m_selectionCtrl->isActive() && event->buttons() & Qt::LeftButton)
            {
                m_selectionCtrl->dragActive(pos);
                return;
            }
            // 未按下：光标反馈
            if (m_canvas.tool == Tool::None)
            {
                const QPointF local = toLocal(pos);
                const bool overSelHandle = m_selectionCtrl->handleAt(pos) != Handle::None;
                if (!m_selection.contains(pos) || overSelHandle)
                {
                    setCursor(SelectionController::cursorShape(m_selectionCtrl->handleAt(pos)));
                }
                else if (m_annotationEditor->hasSelection())
                {
                    // 选中标注：控制点光标 / 项内移动光标
                    setCursor(m_annotationEditor->cursorAt(local));
                }
                else
                {
                    setCursor(m_canvas.hitTest(local) ? Qt::ArrowCursor : Qt::SizeAllCursor);
                }
            }
            else if (m_selection.contains(pos))
            {
                setCursor(m_canvas.tool == Tool::Eraser ? Qt::PointingHandCursor : Qt::CrossCursor);
            }
            else
            {
                setCursor(SelectionController::cursorShape(m_selectionCtrl->handleAt(pos)));
            }
        }
    }

    void CaptureWindow::mouseReleaseEvent(QMouseEvent *event)
    {
        if (event->button() != Qt::LeftButton)
            return;

        if (m_state == State::Selecting)
        {
            const QRect sel = m_selectionCtrl->finishSelect(event->globalPosition().toPoint());
            m_selection = sel;
            // 过小视为取消
            if (sel.width() < kMinSize || sel.height() < kMinSize)
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
            if (m_toolDrawing->isDrawing())
            {
                m_toolDrawing->finish(toLocal(event->globalPosition().toPoint()));
                return;
            }
            m_selectionCtrl->cancelActive();
            m_annotationEditor->endDrag(); // 选中标注保持选中（便于连续操作）
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
                if (m_annotationEditor->tryDoubleClickText(local))
                    return;
            }
            // 选区内其它双击：忽略（防误触——退出截图仅通过 Esc / Enter / 复制、保存按钮）
            return;
        }
        // 双击选区外 → 重新开始框选（重画）
        m_state = State::Selecting;
        m_toolbar->hide();
        m_annotationEditor->clearSelection();
        m_selectionCtrl->beginSelect(pos);
    }

    void CaptureWindow::keyPressEvent(QKeyEvent *event)
    {
        if (event->key() == Qt::Key_Escape)
        {
            if (m_textEditCtrl->isEditing())
            {
                m_textEditCtrl->cancelByEsc(); // 空文本提交 → 删除待输入框 / 标注
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
            if (m_state == State::Editing && !m_textEditCtrl->isEditing())
                confirm();
        }
        else if (event->key() == Qt::Key_Delete)
        {
            // 选择模式：删除选中的标注（输入框内 DEL 由 QTextEdit 自己处理，不介入）
            if (m_state == State::Editing && !m_textEditCtrl->isEditing())
                m_annotationEditor->deleteSelected();
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
        if (m_textEditCtrl->isEditing())
            m_textEditCtrl->commit();

        // 按下回车：仅复制到剪贴板并关闭截图模式（不再弹出结果面板）
        const QPixmap result = m_outputCtrl->render();
        const QRect selLogical = m_selection; // 记录选区位置（供 F3 贴图定位）
        QGuiApplication::clipboard()->setPixmap(result);
        exitCapture();
        emit captured(result, selLogical);
    }

    QRect CaptureWindow::copyToClipboardAndClose()
    {
        if (m_state == State::Idle || m_selection.isEmpty())
            return QRect();
        if (m_textEditCtrl->isEditing())
            m_textEditCtrl->commit();
        // 复制当前截图（含标注）到剪贴板，并关闭截图模式；返回选区位置供贴图定位
        const QRect sel = m_selection;
        QGuiApplication::clipboard()->setPixmap(m_outputCtrl->render());
        exitCapture();
        return sel;
    }

    void CaptureWindow::cancel()
    {
        exitCapture();
        emit captured(QPixmap(), QRect());
    }

    QPointF CaptureWindow::toLocal(const QPoint &global) const
    {
        return QPointF(global - m_selection.topLeft());
    }

} // namespace snipaste
