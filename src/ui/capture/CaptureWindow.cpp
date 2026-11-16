#include "CaptureWindow.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QClipboard>
#include <QLabel>
#include <QMessageBox>

#include "core/annotation/AnnotationPainter.h"
#include "core/capture/QrScanner.h"
#include "core/capture/SmartRectDetector.h"
#include "core/monitor/OwnWriteGuard.h" // T-047-CLIP

#include "ToolBar.h"
#include "CaptureContext.h"
#include "SelectionController.h"
#include "AnnotationEditor.h"
#include "ToolDrawingController.h"
#include "TextEditingController.h"
#include "OcrController.h"
#include "OutputController.h"
#include "OcrPanel.h"

#include "core/translation/Translator.h"

#include "ui/ThemeColors.h"

namespace poptools
{

    // 取色放大卡片几何常量：colorPickerCardRect（几何计算）与 paintColorPicker（绘制）
    // 共用单一数据源（原先两处同名局部常量易失同步）；
    // 155 = 31×5、105 = 21×5 与 paintColorPicker 的 5× 放大倍数自洽
    constexpr int kZoomW = 155, kZoomH = 105, kColorH = 50;

    CaptureWindow::CaptureWindow(RapidOcrService *ocrService, QWidget *parent)
        : QWidget(parent)
    {
        // 全屏无边框置顶窗口
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::BypassWindowManagerHint);
        setCursor(Qt::CrossCursor);
        setMouseTracking(true); // 编辑态下实时反馈控制点光标

        // OCR 引擎：应用级共享实例（子进程昂贵，全局唯一），由组合根注入。
        // 原先"未注入则自建兜底"已删除——它会绕过全局唯一假设，静默拉起
        // 第二个 OCR 引擎子进程；漏注入属组合根装配缺陷，断言在开发期暴露
        m_ocrService = ocrService;
        Q_ASSERT(m_ocrService);
        if (!m_ocrService)
            qWarning("CaptureWindow 未注入共享 OCR 服务，OCR 功能不可用");

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
        // ---- OCR 文字栏（面板模式）与翻译服务（在 selectionChanged 之前创建，避免悬垂访问）----
        m_ocrPanel = std::make_unique<OcrPanel>(this);
        m_ocrPanel->hide();
        m_translator = std::make_unique<Translator>(this);
        // OCR 功能对宿主的窄接口（文字栏面板/扫描动效），独立于 CaptureContext
        OcrHostCallbacks ocrHost;
        ocrHost.showPanel = [this](const QString &text)
        { showOcrPanel(text); };
        // 【译】按钮：OCR 原文自动翻译（目标语言取面板当前选择）
        ocrHost.translateRequested = [this](const QString &text)
        { m_translator->translate(text, m_ocrPanel->sourceLang(), m_ocrPanel->targetLang()); };
        ocrHost.setPanelBusy = [this](bool busy)
        { m_ocrPanel->setBusy(busy); };
        // 文字栏翻译结果区文本（OCR 失败路径清"翻译中…"占位）
        ocrHost.setPanelTranslateText = [this](const QString &text)
        { m_ocrPanel->setTranslateText(text); };
        // OCR 扫描动效：点击【文】时选区内的扫描线，提示用户"没卡死"
        ocrHost.ocrBusy = [this](bool busy) {
            if (busy) {
                m_ocrScanning = true;
                m_ocrScanPos = 0.0;
                m_ocrScanTimer.start();
            } else {
                m_ocrScanning = false;
                m_ocrScanTimer.stop();
                update();
            }
        };
        // 面板内"翻译"按钮：手动重新翻译（源/目标语言取自面板下拉）
        QObject::connect(m_ocrPanel.get(), &OcrPanel::translateRequested, this,
                         [this](const QString &text, const QString &fromLang, const QString &toLang)
                         {
                             m_ocrPanel->setBusy(true);
                             m_translator->translate(text, fromLang, toLang);
                         });

        m_ctx.stateChanged = [this]
        {
            m_toolbar->setUndoRedoEnabled(m_canvas.canUndo(), m_canvas.canRedo());
            update();
        };
        m_ctx.selectionChanged = [this]
        {
            positionToolbar();   // 选区移动/选中项变化 → 工具栏跟随
            if (m_ocrPanel->isVisible())
                positionOcrPanel(); // 文字栏同样跟随选区移动
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
        m_ocrCtrl = std::make_unique<OcrController>(&m_ctx, m_ocrService, std::move(ocrHost));
        m_outputCtrl = std::make_unique<OutputController>(&m_ctx);
        m_textEditCtrl->init(this); // 创建文字内嵌输入框
        QObject::connect(m_translator.get(), &Translator::translateFinished, this,
                         [this](bool ok, const QString &result, const QString &error)
                         {
                             m_ocrPanel->setBusy(false);
                             m_ocrPanel->setTranslateText(ok ? result
                                                             : QStringLiteral("翻译失败：%1").arg(error));
                         });

        // ---- OCR 扫描动效定时器（动效开关见 ocrHost.ocrBusy） ----
        m_ocrScanTimer.setInterval(30);
        connect(&m_ocrScanTimer, &QTimer::timeout, this, [this] {
            const qreal oldPos = m_ocrScanPos;
            m_ocrScanPos += 0.018; // 一次完整扫描约 1.7 秒
            if (m_ocrScanPos > 1.0)
                m_ocrScanPos = 0.0; // 循环扫描
            // 只重绘扫描带（新旧位置并集，±6px 覆盖 4px 光晕 + 2px 线），
            // 原先每 30ms 全窗 update，4K 下动效期间 GPU/光栅化持续满载
            if (m_state == State::Editing && !m_selection.isEmpty())
            {
                const QRect sel = m_selection.translated(-m_cap.desktop.topLeft());
                const auto band = [sel](qreal pos)
                {
                    return QRect(sel.left(), sel.top() + qRound(sel.height() * pos) - 6,
                                 sel.width(), 12);
                };
                update(band(oldPos).united(band(m_ocrScanPos)));
            }
        });

        // ---- ESC 退出确认定时器：武装态 1 秒无后续按键自动解除，提示不常驻 ----
        connect(&m_exitArmTimer, &QTimer::timeout, this, &CaptureWindow::disarmExitConfirm);

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
                    m_ctx.stateChanged(); // 撤销/重做收口：按钮态同步 + 重绘（与键盘分支共用）
                });
        connect(m_toolbar, &ToolBar::redoRequested, this, [this]
                {
                    m_textEditCtrl->commit();
                    m_canvas.redo();
                    m_ctx.stateChanged(); // 撤销/重做收口：按钮态同步 + 重绘（与键盘分支共用）
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
        connect(m_toolbar, &ToolBar::translateRequested, this, &CaptureWindow::onTranslateRequested);
        connect(m_toolbar, &ToolBar::qrRequested, this, &CaptureWindow::onQrRequested);

        // 智能框选检测定时器（40ms）：与鼠标事件解耦，检测不阻塞取色卡片跟随。
        // 探测器持常驻工作线程（COM/UIA），构造即启动，随窗口销毁
        m_smartDetectTimer.setInterval(40);
        connect(&m_smartDetectTimer, &QTimer::timeout, this, &CaptureWindow::onSmartDetect);
        m_smartDetector = new SmartRectDetector(this);
        connect(m_smartDetector, &SmartRectDetector::found, this, &CaptureWindow::onSmartFound);
    }

    // ==================== 截图生命周期 ====================

    void CaptureWindow::startCapture()
    {
        // 捕获所有屏幕拼接的物理分辨率画面（多屏扩展无黑屏），
        // 并记录逻辑/物理虚拟桌面几何（窗口/鼠标/选区均用逻辑坐标）
        m_cap = ScreenCapturer::capture();

        m_state = State::Selecting;
        resetSelectionSession(); // 选区会话公共复位（含双击重选标志，T-062 修复新增项）
        m_screenImage = QImage(); // 取色缓存失效（screen 画面已更新）
        m_basePixmap = QPixmap(); // R6-B1: 底图缓存失效（screen 画面已更新）
        m_lastColorCardRect = QRect(); // 取色卡片跟随局部重绘状态复位
        m_smartMousePos = QCursor::pos(); // 检测起点为当前鼠标位置
        m_smartLastPos = m_smartMousePos;
        ++m_smartGen; // 新捕获代次，丢弃仍在途的旧检测结果
        m_smartDetectTimer.start();
        m_canvas.clear();
        m_canvas.tool = Tool::None; // 默认选择模式
        m_toolDrawing->setTool(Tool::None); // 清空上一轮工具实例
        m_selectionCtrl->cancelActive();
        m_annotationEditor->clearSelection();
        m_ocrScanning = false; // 重置 OCR 扫描动效
        m_ocrScanTimer.stop();
        m_toolbar->hide();
        // OCR 文字栏无条件隐藏兜底（R5-B1）：退出会话后迟到的【译】回包曾把面板
        // visible 置位（子控件标志在窗口隐藏时保留），重进截图时随窗口映射浮出
        // 上一会话的 OCR 内容；setBusy(false) 同时解除可能卡住的禁用态
        m_ocrPanel->hide();
        m_ocrPanel->setBusy(false);
        setGeometry(m_cap.desktop);
        show();
        activateWindow();
        raise();
        setFocus();
    }

    void CaptureWindow::exitCapture()
    {
        m_state = State::Idle;
        resetSelectionSession(); // 选区会话公共复位（含双击重选标志，T-062 修复新增项）
        m_screenImage = QImage(); // 释放取色缓存
        m_basePixmap = QPixmap(); // R6-B1: 释放底图缓存（约 8-33MB，空闲期不留驻）
        m_ocrCtrl->releaseSessionResources(); // 释放 OCR 选区大图缓存（红线 5，与上面两处并列）
        // T-062（R1 结构 12）：主画面同样空闲期不留驻。m_cap.screen 是全桌面物理
        // 拼图（8-33MB 量级），与上面各缓存口径统一；隐藏期间所有 m_cap 消费点
        // （paintEvent/取色/render/OCR 裁剪/马赛克底图）均在会话态内才可达，
        // 下一轮 startCapture 首行重新赋值。captured() 携带的 QPixmap 与剪贴板
        // 副本走 COW，源清空不影响已发布的结果图
        m_cap = CaptureResult{};
        m_smartDetectTimer.stop(); // 停止智能框选检测
        m_ocrScanning = false; // 停止 OCR 扫描动效
        m_ocrScanTimer.stop();
        m_toolbar->hide();
        m_ocrPanel->hide(); // 隐藏 OCR 文字栏
        m_selectionCtrl->cancelActive();
        m_annotationEditor->clearSelection();
        m_textEditCtrl->detach(); // 文字编辑态清理（隐藏输入框）
        disarmExitConfirm();      // ESC 退出确认武装态与轻提示一并清理
        hide();
    }

    // 选区会话状态复位：startCapture（进入前）与 exitCapture（退出后）逐字段相同的
    // 公共子集；两处各自特有的复位项（缓存失效/定时器停止等）不在此处
    void CaptureWindow::resetSelectionSession()
    {
        m_selection = QRect();
        m_dblReselect = false;       // 双击重画待吞 release 状态复位
        m_reselecting = false;       // 双击重选"过小恢复"标志复位（T-062 修复新增）
        m_reselectSaved = QRect();   // 重选暂存选区失效（T-062 修复新增）
        m_smartRect = QRect();       // 智能框选高亮复位
        m_smartPressPending = false; // 预高亮点选判定复位
        m_smartPressRect = QRect();
    }

    void CaptureWindow::enterEditingState()
    {
        m_state = State::Editing;
        m_smartRect = QRect(); // 进入编辑态清除智能框选高亮
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
        // 线宽档位值随工具变化；工具栏程序性刷新不发 widthSelected，这里主动同步
        m_canvas.width = m_toolbar->currentWidth();
        m_toolDrawing->setTool(t); // 工厂创建工具实例（None=选择模式）
        m_annotationEditor->clearSelection(); // 切换工具取消标注选中 + 字号栏刷新
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

    // 【译】：识别并翻译。面板先显示"翻译中…"，识别完成自动打开面板、回填原文并发起翻译
    void CaptureWindow::onTranslateRequested()
    {
        if (m_state != State::Editing || m_selection.isEmpty())
            return;
        if (m_textEditCtrl->isEditing())
            m_textEditCtrl->commit();
        m_ocrPanel->setTranslateText(QStringLiteral("翻译中…"));
        m_ocrPanel->setBusy(true); // 后置：禁用复制，避免复制到"翻译中…"占位文本
        m_ocrCtrl->requestTranslate(this);
    }

    // 【码】：识别选区内二维码。结果弹窗展示并复制到剪贴板（多码逐行）；
    // 组件缺失（tools/zxing 未安装）时提示安装脚本
    void CaptureWindow::onQrRequested()
    {
        if (m_state != State::Editing || m_selection.isEmpty())
            return;
        if (!qr::available())
        {
            QMessageBox::information(this, QStringLiteral("二维码识别"),
                QStringLiteral("二维码组件未安装：运行 scripts/install_zxing.py 下载 "
                               "zxing-cpp 后重新构建即可启用。"));
            return;
        }
        // 选区物理像素裁剪（与 OCR 同源），识别（支持一图多码）
        const QRect physCrop = ScreenCapturer::toPhysicalRect(m_selection)
                                   .translated(-m_cap.physDesktop.topLeft());
        const QStringList codes = qr::scan(m_cap.screen.copy(physCrop).toImage());
        if (codes.isEmpty())
        {
            QMessageBox::information(this, QStringLiteral("二维码识别"),
                                     QStringLiteral("选区内未识别到二维码。"));
            return;
        }
        const QString text = codes.join(QLatin1Char('\n'));
        QGuiApplication::clipboard()->setText(text);
        OwnWriteGuard::noteSelfWrite(); // T-047-CLIP：自写排除
        QMessageBox::information(this, QStringLiteral("二维码识别"),
                                 text + QStringLiteral("\n\n—— 内容已复制到剪贴板"));
    }

    // 面板模式：OCR 识别文字显示到文字栏（清空上次翻译，重新定位）
    void CaptureWindow::showOcrPanel(const QString &text)
    {
        m_ocrPanel->setOcrText(text);
        m_ocrPanel->setTranslateText(QString());
        positionOcrPanel();
        m_ocrPanel->show();
        m_ocrPanel->raise();
    }

    // 文字栏定位：宽度至少 10 字符；优先放选区外部（右侧 → 上方），放不下则选区内（内部兜底）
    void CaptureWindow::positionOcrPanel()
    {
        m_ocrPanel->adjustSize();
        const int minW = m_ocrPanel->fontMetrics().horizontalAdvance(QLatin1Char('0')) * 10;
        if (m_ocrPanel->width() < minW)
            m_ocrPanel->resize(minW, m_ocrPanel->height());

        const QRect sel = m_selection.translated(-m_cap.desktop.topLeft());
        // 尝试选区右侧（外部）
        QPoint pos = sel.topRight() + QPoint(8, 0);
        if (pos.x() + m_ocrPanel->width() > width())
        {
            // 右侧放不下 → 尝试选区上方（外部）
            pos = QPoint(sel.left(), sel.top() - m_ocrPanel->height() - 8);
            if (pos.y() < 0)
                pos = sel.topLeft(); // 上方也放不下 → 选区内（内部兜底）
        }
        // 最终夹紧在窗口内
        pos.setX(qBound(0, pos.x(), qMax(0, width() - m_ocrPanel->width())));
        pos.setY(qBound(0, pos.y(), qMax(0, height() - m_ocrPanel->height())));
        m_ocrPanel->move(pos);
    }

    // ==================== 绘制编排 ====================

    void CaptureWindow::paintEvent(QPaintEvent *event)
    {
        Q_UNUSED(event)
        QPainter p(this);
        // R6-B1: 底图 1:1 直绘。旧实现把物理分辨率原图 Smooth 缩到逻辑尺寸缓存，
        // drawPixmap 时又被设备像素比放大回屏幕——双重重采样，是 150% DPI 下
        // 冻结背景糊屏的根因（P-035）。现改为物理原图挂当前屏幕 DPR 元数据后
        // 直接绘制，物理像素与设备像素一一对应，零重采样；每帧不再有大图变换，
        // 悬停/动效的局部重绘仍以本缓存为前提。失效条件：捕获变更（重进截图
        // 时置空）‖ 逻辑尺寸变更 ‖ DPR 变化（跨屏移动）‖ 物理/逻辑尺寸不匹配
        // （混合 DPR 多屏 → 每次缓存重建时重判走回退分支，见 R6-B1a）
        const qreal dpr = devicePixelRatioF();
        if (m_basePixmap.isNull() || m_baseDpr != dpr || rect().size() != m_baseLogicalSize)
        {
            // R6-B1a: 混合 DPR 多屏守卫。ScreenCapturer 各屏按自身 DPR 拼接物理图，
            // 窗口却只有一个 DPR：物理尺寸 ≠ rect()×dpr 时 1:1 直绘会让底图溢出
            // 被裁剪（窗口 DPR 取低屏）或右/下露底（取高 DPR 屏）。此时回退旧策略：
            // Smooth 缩到逻辑尺寸且不挂 DPR 元数据，drawPixmap(0,0) 按逻辑尺寸
            // 铺满窗口——糊但全覆盖；尺寸匹配（单屏/同 DPR，容差吸收 qRound 边缘
            // 取整）维持 1:1 直绘。守卫在缓存重建时判定，尺寸/DPR 变化自然触发重判
            const QSize phys = m_cap.screen.size();
            const bool physMatch = qAbs(phys.width() - rect().width() * dpr) <= 2.0 &&
                                   qAbs(phys.height() - rect().height() * dpr) <= 2.0;
            if (physMatch)
            {
                m_basePixmap = m_cap.screen; // 物理分辨率原图（ScreenCapturer 产出，dpr=1）
                m_basePixmap.setDevicePixelRatio(dpr); // 1:1 直绘：物理像素 ↔ 设备像素
            }
            else
            {
                m_basePixmap = m_cap.screen.scaled(rect().size(), Qt::IgnoreAspectRatio,
                                                   Qt::SmoothTransformation);
                m_basePixmap.setDevicePixelRatio(1.0); // 不挂 DPR：按逻辑尺寸铺满窗口
            }
            m_baseDpr = dpr;
            m_baseLogicalSize = rect().size();
        }
        p.drawPixmap(0, 0, m_basePixmap); // 底层为原始屏幕画面（直绘 1:1 / 回退铺满共用此调用点）

        if (m_state == State::Idle)
            return;

        // 灰色蒙版 + 框选虚线框（Selecting 态）；预高亮区域在蒙版中挖空（全透明）。
        // 按下待判定期改挖空按下的预选矩形：按下瞬间蒙版仍在（半透明），
        // 被按住的窗口保持清晰——松手即点选该区域，拖动则无缝转手动框选
        QRect smartWin;
        if (m_state == State::Selecting)
        {
            if (m_smartPressPending && !m_smartPressRect.isEmpty())
                smartWin = m_smartPressRect.translated(-m_cap.desktop.topLeft());
            else if (!m_smartRect.isEmpty())
                smartWin = m_smartRect.translated(-m_cap.desktop.topLeft());
        }
        m_selectionCtrl->paintMaskAndFrame(p, rect(), m_state == State::Selecting, smartWin);

        // 智能框选：预先高亮鼠标下的窗口矩形（橙色虚线，区别于框选蓝虚线）
        if (m_state == State::Selecting && !m_smartRect.isEmpty())
        {
            const QRect sr = m_smartRect.translated(-m_cap.desktop.topLeft());
            p.setPen(QPen(overlay::smartOrange(), 2, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(sr);
        }

        // 取色放大卡片：Selecting 态（含拖动框选）/ Editing 态调整选区控制点时显示（置顶）
        if (m_state == State::Selecting ||
            (m_state == State::Editing && m_selectionCtrl->resizing()))
            paintColorPicker(p);

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

        // ESC 二次退出提示已改为 armExitConfirm 的轻提示胶囊（QLabel 子控件，
        // 有墨迹时才出现），不再走 paintEvent 绘制

        // ---- OCR 扫描动效：选区内的扫描线（提示用户 OCR 正在运行） ----
        if (m_ocrScanning && m_state == State::Editing && !m_selection.isEmpty())
        {
            const QRect sel = m_selection.translated(-m_cap.desktop.topLeft());
            const int scanY = sel.top() + qRound(sel.height() * m_ocrScanPos);

            // 发光带：扫描线上下各 4px 的半透明渐变（主色蓝，替代旧 (0,150,255)）
            const QColor scanBlue = overlay::accent();
            QColor glowEdge = scanBlue;  glowEdge.setAlpha(0);
            QColor glowCore = scanBlue;  glowCore.setAlpha(50);
            QColor scanLine = scanBlue;  scanLine.setAlpha(200);
            QLinearGradient glow(0, scanY - 4, 0, scanY + 4);
            glow.setColorAt(0, glowEdge);
            glow.setColorAt(0.4, glowCore);
            glow.setColorAt(0.6, glowCore);
            glow.setColorAt(1, glowEdge);
            p.fillRect(sel.left(), scanY - 4, sel.width(), 8, glow);

            // 扫描线：2px 实线
            p.setPen(QPen(scanLine, 2));
            p.drawLine(sel.left(), scanY, sel.right(), scanY);
        }
    }

    // ==================== 事件路由 ====================

    void CaptureWindow::mousePressEvent(QMouseEvent *event)
    {
        // 武装态下点击鼠标（任意键）：解除退出确认，本次点击按原语义正常处理。
        // 先快照解除前的武装态：下方右键"再按一次才退出"的判定必须用入口值——
        // T-062 修复（R1 疑似BUG 2）：原先在解除后读 m_exitArmActive（恒 false），
        // 武装态下无论再右键多少次都只是反复重新武装，永远无法退出
        const bool wasArmed = m_exitArmActive;
        if (m_exitArmActive)
            disarmExitConfirm();

        const QPoint pos = event->globalPosition().toPoint();

        if (event->button() == Qt::RightButton)
        {
            // 拖拽绘制中右键 = 取消当前笔画（R5-B2：与 Esc 同语义，工具与截图保留；
            // 绘制中右键无菜单语义——mousePressEvent 是全窗口唯一 RightButton 处理点）
            if (m_state == State::Editing && m_toolDrawing->isDrawing())
            {
                m_toolDrawing->abortDraft();
                return;
            }
            // 编辑态有墨迹时右键选区外退出与 ESC 保护对齐：首次武装确认，
            // 再按（含再右键）才退出，防误触一键清掉全部标注
            if (m_state == State::Editing && !m_selection.contains(pos) && hasInk())
            {
                handleExitIntent(wasArmed);
                return;
            }
            // 与左键一致：仅点击选区外退出（Selecting 态尚无选区，右键任意处退出）
            if (m_state == State::Selecting || !m_selection.contains(pos))
                cancel();
            return;
        }

        if (event->button() != Qt::LeftButton)
            return;

        if (m_state == State::Selecting)
        {
            // 智能框选：按下预高亮区域内 → 暂不进入编辑态，先判定"点选"还是"按住拖动自选"
            if (!m_smartRect.isEmpty() && m_smartRect.contains(pos))
            {
                m_smartPressPending = true;
                m_smartPressRect = m_smartRect;
                m_smartPressPos = pos;
                const QRect oldWin = m_smartRect.translated(-m_cap.desktop.topLeft());
                m_smartRect = QRect(); // 取消预高亮高亮显示，待拖动判定
                m_smartGen++;          // 使在途检测结果失效，防止拖动中预高亮闪回
                // 蒙版挖空区不变（转入按下矩形），仅橙色虚线框消失 → 只刷该矩形
                update(oldWin);
                return;
            }
            // 预高亮区域外按下：清除高亮，直接开始手动框选
            if (!m_smartRect.isEmpty())
            {
                const QRect oldWin = m_smartRect.translated(-m_cap.desktop.topLeft());
                m_smartRect = QRect();
                m_smartGen++;
                update(oldWin);
            }
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

        if (m_state == State::Selecting)
        {
            if (event->buttons() & Qt::LeftButton)
            {
                // 按下预高亮后拖动：超过阈值 → 取消点选判定，转为普通框选（以按下点为锚）
                if (m_smartPressPending)
                {
                    const QPoint d = pos - m_smartPressPos;
                    if (qAbs(d.x()) + qAbs(d.y()) > QApplication::startDragDistance())
                    {
                        m_smartPressPending = false;
                        m_smartPressRect = QRect();
                        m_selectionCtrl->beginSelect(m_smartPressPos);
                        m_selectionCtrl->dragSelect(pos);
                    }
                    return;
                }
                m_selectionCtrl->dragSelect(pos);
                return;
            }
            // 记录鼠标位置供智能框选定时器检测；取色卡片跟随改为局部重绘
            // （新旧卡片矩形并集 + 阴影余量），原先每次移动全窗 update
            m_smartMousePos = pos;
            const QRect newCard = colorPickerCardRect(pos);
            // 阴影余量（R2 实测：两层阴影均只向左/右/下越出卡片——近层 左1/右1/下4，
            // 远层 左3/右3/下12，顶部无越界）：左 4 / 上 4 为安全余量（上向纯冗余），
            // 右 6 容右向 +3。T-062 修复（R2-F2）：下余量 8→13（12px 越界 + 1px 抗锯齿），
            // 原值挡不住远层阴影下越界，卡片上移时旧阴影尾迹留 4px 淡残影带
            //（既有重绘缺口，一并收口）
            update(m_lastColorCardRect.united(newCard).adjusted(-4, -4, 6, 13));
            m_lastColorCardRect = newCard;
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
            // 双击重画的尾随 release：吞掉并以释放点为新起点（见 mouseDoubleClickEvent）
            if (m_dblReselect)
            {
                m_dblReselect = false;
                m_selectionCtrl->cancelActive();
                m_selectionCtrl->beginSelect(event->globalPosition().toPoint());
                return;
            }
            // 智能框选点选：按下预高亮且未拖动 → 直接框选该区域
            if (m_smartPressPending)
            {
                m_smartPressPending = false;
                if (!m_smartPressRect.isEmpty())
                {
                    const QRect r = m_smartPressRect;
                    m_smartPressRect = QRect();
                    m_selection = r;
                    m_reselecting = false; // 点选成功 = 重选完成（若本次处于重选路径）
                    m_reselectSaved = QRect(); // R2-F1：成功即作废暂存（与失败分支对称）
                    enterEditingState();
                    return;
                }
            }
            const QRect sel = m_selectionCtrl->finishSelect(event->globalPosition().toPoint());
            // 过小视为取消。T-062 修复（R1 疑似BUG 6，行为变化）：双击重画路径
            // （m_reselecting）带全部标注进入本出口，原先一次 <8px 的误拖会整场
            // cancel 丢弃所有标注（首选拖选时无可丢失内容故合理）；现改为恢复
            // 原选区编辑态（m_reselectSaved 为 beginSelect 覆写前的旧选区）。
            // 首个框选路径语义不变：过小仍整场取消
            if (sel.width() < kMinSize || sel.height() < kMinSize)
            {
                if (m_reselecting)
                {
                    m_reselecting = false;
                    m_selection = m_reselectSaved; // 恢复原选区（finishSelect 结果不采纳）
                    m_reselectSaved = QRect();
                    m_state = State::Editing;
                    m_selectionCtrl->cancelActive();
                    positionToolbar();
                    m_toolbar->show();
                    update();
                    return;
                }
                m_selection = sel;
                cancel();
                return;
            }
            m_selection = sel;
            m_reselecting = false;     // 重选成功完成（若本次处于重选路径）
            m_reselectSaved = QRect(); // R2-F1：成功即作废暂存（与失败分支对称）
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
            update(); // 松开控制点：立即重绘，取色卡片（resizing 已复位）随之消失
        }
    }

    // 智能框选检测定时器回调：鼠标位置变化时，向探测器提交"鼠标下元素矩形"查询。
    // 探测器在工作线程执行（Win32 枚举 + UIA 控件树），latest-wins，静止时零提交
    void CaptureWindow::onSmartDetect()
    {
        if (m_state != State::Selecting || m_smartPressPending)
            return;
        // 左键按住（拖动框选中）不再提交：否则按下瞬间未消费的鼠标位移会在
        // 拖动中触发一次提交，橙色预选框回闪，与蓝色选框并存
        if (QApplication::mouseButtons() & Qt::LeftButton)
            return;
        if (m_smartMousePos == m_smartLastPos)
            return; // 位置未变，跳过提交（静止时零开销）
        m_smartLastPos = m_smartMousePos;

        const QScreen *scr = QGuiApplication::screenAt(m_smartMousePos);
        const qreal dpr = scr ? scr->devicePixelRatio() : 1.0;
        m_smartDetector->submit(ScreenCapturer::toPhysical(m_smartMousePos), dpr,
                                quint64(m_smartGen), reinterpret_cast<void *>(winId()));
    }

    // 检测结果回包：代次过期或鼠标已移走的结果丢弃，其余生效为预选高亮
    void CaptureWindow::onSmartFound(quint64 gen, const QRect &rect)
    {
        if (gen != quint64(m_smartGen))
            return;
        // 回包时已不在选择态或左键按住（提交与按下之间的竞态）：丢弃，
        // 防止手动框选拖动中橙色预选框回闪、与蓝色选框并存
        if (m_state != State::Selecting || (QApplication::mouseButtons() & Qt::LeftButton))
            return;
        if (!rect.isEmpty() && !rect.contains(m_smartMousePos))
            return;
        if (rect != m_smartRect)
        {
            const QRect oldWin = m_smartRect.translated(-m_cap.desktop.topLeft());
            m_smartRect = rect;
            // 局部重绘：新旧预选矩形并集（蒙版挖空区变化 + 2px 虚线 + 抗锯齿余量）
            update(oldWin.united(rect.translated(-m_cap.desktop.topLeft()))
                       .adjusted(-3, -3, 3, 3));
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
        // 双击选区外 → 重新开始框选（重画）。第二击的 release 稍后到达，
        // 由 m_dblReselect 吞掉（否则 1px 退化矩形触发"过小视为取消"秒退）。
        // 注意 beginSelect 会立即用新框选矩形覆写 m_selection，旧选区须先暂存
        // 到 m_reselectSaved：重选失败（过小释放，见 mouseReleaseEvent T-062 修复）
        // 时据此恢复原选区编辑态；旧标注在重选期间仍在画布上，随编辑态一并恢复显示
        m_state = State::Selecting;
        m_dblReselect = true;
        m_reselecting = true;          // T-062 修复（R1 疑似BUG 6）：标记重选路径
        m_reselectSaved = m_selection; // 暂存旧选区（beginSelect 覆写前）
        m_toolbar->hide();
        m_ocrPanel->hide(); // 重选过程中旧 OCR 面板不再悬浮（R5-B1 附带：旧面板内容已过时）
        m_annotationEditor->clearSelection();
        m_selectionCtrl->beginSelect(pos);
    }

    void CaptureWindow::keyPressEvent(QKeyEvent *event)
    {
        // 武装态下按其他键：解除退出确认并按原语义处理（Enter 仍是确认截图等）
        if (m_exitArmActive && event->key() != Qt::Key_Escape)
            disarmExitConfirm();

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
            // 正在拖拽绘制：仅取消当前笔画（工具与截图保留），不退出
            if (m_toolDrawing->isDrawing())
            {
                m_toolDrawing->abortDraft();
                return;
            }
            // 画布有墨迹（标注 / OCR 文字框）：首次 ESC 武装防误触，再次 ESC 才退出；
            // 无墨迹时没有可丢失的内容，直接退出零摩擦。
            // ESC 分支顶部只解除"非 ESC 键"的武装，此处 m_exitArmActive 即入口值
            if (hasInk())
            {
                handleExitIntent(m_exitArmActive);
                return;
            }
            cancel();
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
        else if (event->key() == Qt::Key_Z && (event->modifiers() & Qt::ControlModifier)
                 && !(event->modifiers() & Qt::ShiftModifier)) // Ctrl+Shift+Z 让位给重做别名
        {
            // 撤销（Ctrl+Z）：编辑态生效；文字输入中由 QTextEdit 自理，拖拽绘制中不介入
            if (m_state == State::Editing && !m_textEditCtrl->isEditing() && !m_toolDrawing->isDrawing()
                && m_canvas.canUndo())
            {
                m_canvas.undo();
                m_ctx.stateChanged(); // 与工具栏撤销槽同一收口：按钮态同步 + 重绘
            }
        }
        else if ((event->modifiers() & Qt::ControlModifier)
                 && (event->key() == Qt::Key_Y
                     || (event->key() == Qt::Key_Z && (event->modifiers() & Qt::ShiftModifier))))
        {
            // 重做（Ctrl+Y，兼容通用别名 Ctrl+Shift+Z）：条件同撤销
            if (m_state == State::Editing && !m_textEditCtrl->isEditing() && !m_toolDrawing->isDrawing()
                && m_canvas.canRedo())
            {
                m_canvas.redo();
                m_ctx.stateChanged(); // 与工具栏重做槽同一收口：按钮态同步 + 重绘
            }
        }
        else if (event->key() == Qt::Key_C && event->modifiers() == Qt::NoModifier)
        {
            // 取色：复制鼠标下像素的 16 进制颜色到剪贴板，并直接退出截图模式。
            // 要求无修饰键：Ctrl+C 留给"复制截图"语义，不再误触取色
            if (m_state == State::Selecting && !(QApplication::mouseButtons() & Qt::LeftButton))
            {
                const QColor c = screenColorAt(QCursor::pos());
                if (c.isValid())
                {
                    QGuiApplication::clipboard()->setText(c.name(QColor::HexRgb).toUpper());
                    OwnWriteGuard::noteSelfWrite(); // T-047-CLIP：自写排除
                }
                cancel();
            }
        }
    }

    // 窗口尺寸变化 → 工具栏跟随重定位；截图窗几何由 startCapture 一次设定，
    // 常规截图流程不触发（罕见于系统 DPI/重排事件）
    void CaptureWindow::resizeEvent(QResizeEvent *event)
    {
        QWidget::resizeEvent(event);
        if (m_toolbar->isVisible())
            positionToolbar();
    }

    // ==================== 确认 / 取消 ====================

    void CaptureWindow::confirm()
    {
        // 拖拽绘制中忽略确认：半截草稿不该被合成进输出（等松手后再按 Enter）
        if (m_selection.isEmpty() || m_toolDrawing->isDrawing())
            return;
        if (m_textEditCtrl->isEditing())
            m_textEditCtrl->commit();

        // 按下回车：仅复制到剪贴板并关闭截图模式（不再弹出结果面板）
        const QPixmap result = m_outputCtrl->render();
        const QRect selLogical = m_selection; // 记录选区位置（供 F3 贴图定位）
        QGuiApplication::clipboard()->setPixmap(result);
        OwnWriteGuard::noteSelfWrite(); // T-047-CLIP：自写排除
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
        OwnWriteGuard::noteSelfWrite(); // T-047-CLIP：自写排除
        exitCapture();
        return sel;
    }

    void CaptureWindow::cancel()
    {
        exitCapture();
        emit captured(QPixmap(), QRect());
    }

    // ==================== ESC 退出确认（有墨迹时防误触） ====================

    void CaptureWindow::armExitConfirm()
    {
        m_exitArmActive = true;
        if (!m_escToast)
        {
            // 胶囊提示：深色底白字在任何截图内容上都可读；不拦鼠标不挡操作
            // （P-022/P-023：样式写在控件自身，objectName ID 选择器）
            m_escToast = new QLabel(this);
            m_escToast->setObjectName(QStringLiteral("escExitToast"));
            m_escToast->setStyleSheet(QStringLiteral(
                "QLabel#escExitToast {"
                "  background: rgba(28, 28, 30, 235);"
                "  color: #FFFFFF;"
                "  border: 1px solid rgba(255, 255, 255, 45);"
                "  border-radius: 6px;"
                "  padding: 7px 16px;"
                "  font-size: 13px;"
                "}"));
            m_escToast->setAttribute(Qt::WA_TransparentForMouseEvents);
        }
        m_escToast->setText(QStringLiteral("再按一次 ESC 退出截图 · 标注将丢弃"));
        m_escToast->adjustSize();
        positionEscToast();
        m_escToast->show();
        m_escToast->raise();
        m_exitArmTimer.start(1000); // 1 秒内未再按 ESC 自动解除，提示不常驻
    }

    void CaptureWindow::disarmExitConfirm()
    {
        m_exitArmActive = false;
        m_exitArmTimer.stop();
        if (m_escToast)
            m_escToast->hide();
    }

    // 武装退出判定：ESC 与右键选区外两条路径的共用出口（原先两处逐字重复）。
    // wasArmed 语义见头文件声明——必须传事件入口处的武装态快照
    void CaptureWindow::handleExitIntent(bool wasArmed)
    {
        if (wasArmed)
            cancel();
        else
            armExitConfirm();
    }

    void CaptureWindow::positionEscToast()
    {
        if (!m_escToast)
            return;
        // 工具栏上缘居中（用户视线锚点在工具栏）；贴近窗口上缘放不下转下方
        const QRect tb = m_toolbar->geometry();
        int x = tb.center().x() - m_escToast->width() / 2;
        int y = tb.top() - m_escToast->height() - 6;
        if (y < 0)
            y = tb.bottom() + 6;
        x = qBound(0, x, width() - m_escToast->width());
        y = qBound(0, y, height() - m_escToast->height());
        m_escToast->move(x, y);
    }

    QPointF CaptureWindow::toLocal(const QPoint &global) const
    {
        return QPointF(global - m_selection.topLeft());
    }

    // ==================== 取色放大卡片 ====================

    QColor CaptureWindow::screenColorAt(const QPoint &logicalGlobal) const
    {
        if (m_screenImage.isNull())
            m_screenImage = m_cap.screen.toImage(); // 懒加载：Selecting 全程复用同一张画面
        if (m_screenImage.isNull())
            return QColor();
        const QPoint phys = ScreenCapturer::toPhysical(logicalGlobal) - m_cap.physDesktop.topLeft();
        if (!QRect(0, 0, m_screenImage.width(), m_screenImage.height()).contains(phys))
            return QColor();
        return m_screenImage.pixelColor(phys);
    }

    // 取色卡片几何：鼠标右下 (20,20)，越界翻转到左上，最终夹紧窗口内。
    // 绘制（paintColorPicker）与悬停局部重绘（mouseMoveEvent）共用同一套逻辑，
    // 保证局部重绘算出的矩形与实际绘制完全一致
    QRect CaptureWindow::colorPickerCardRect(const QPoint &pos) const
    {
        const QSize cardSize(kZoomW, kZoomH + kColorH); // 几何常量见文件顶部（单一数据源）

        QPoint cardPos = pos - m_cap.desktop.topLeft() + QPoint(20, 20);
        if (cardPos.x() + cardSize.width() > width())
            cardPos.setX(cardPos.x() - cardSize.width() - 40);
        if (cardPos.y() + cardSize.height() > height())
            cardPos.setY(cardPos.y() - cardSize.height() - 40);
        cardPos.setX(qBound(0, cardPos.x(), width() - cardSize.width()));
        cardPos.setY(qBound(0, cardPos.y(), height() - cardSize.height()));
        return QRect(cardPos, cardSize);
    }

    void CaptureWindow::paintColorPicker(QPainter &p) const
    {
        // 显示范围由 paintEvent 决定（Selecting 任意态 / Editing 调整选区控制点）：
        // 拖动框选、调整控制点时同样显示，故不在此处排除左键按下

        const QPoint pos = QCursor::pos(); // 全局逻辑坐标（鼠标箭头位置）
        const QColor center = screenColorAt(pos);
        if (!center.isValid())
            return;

        // 屏幕物理像素坐标（取色放大卡片跟随鼠标）
        const QPoint phys = ScreenCapturer::toPhysical(pos) - m_cap.physDesktop.topLeft();
        const QPoint c(qBound(0, phys.x(), m_screenImage.width() - 1),
                       qBound(0, phys.y(), m_screenImage.height() - 1));

        // 源区域：中心左右 ±15、上下 ±10（31×21 物理像素），夹紧到画面内
        QRect src(c.x() - 15, c.y() - 10, 31, 21);
        src = src.intersected(QRect(0, 0, m_screenImage.width(), m_screenImage.height()));
        if (src.isEmpty())
            return;

        // 卡片几何（绘制与悬停局部重绘共用一套定位逻辑）
        const int kZoom = 5;
        const QRect cardRect = colorPickerCardRect(pos);

        // 卡片阴影（双层近似高斯：近层实影 + 远层扩散），先于卡片内容绘制
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 60));
        p.drawRoundedRect(QRectF(cardRect).translated(0, 3).adjusted(-1, -1, 1, 1), 12, 12);
        p.setBrush(QColor(0, 0, 0, 30));
        p.drawRoundedRect(QRectF(cardRect).translated(0, 9).adjusted(-3, -3, 3, 3), 14, 14);

        // ---- 卡片主体：12px 圆角，放大区经路径裁剪贴合圆角 ----
        QPainterPath cardPath;
        cardPath.addRoundedRect(QRectF(cardRect), 12, 12);

        // ---- 上 2/3：放大区域 ----
        const QRect zoomRect(cardRect.left(), cardRect.top(), kZoomW, kZoomH);
        const QImage crop = m_screenImage.copy(src);
        p.save();
        p.setClipPath(cardPath);
        // R6-B1: 按设备像素尺寸做最近邻重采样后挂 DPR 直绘。旧实现缩到逻辑
        // 尺寸再由绘制系统二次拉伸到设备像素，150% 下 5×1.5=7.5 非整数块宽
        // 交替不均；一次重采样直绘消除二次插值。Fast（最近邻）是取色卡的
        // 功能正确选择——色块需要硬边，不能换回 Smooth
        const qreal zoomDpr = devicePixelRatioF();
        QPixmap zoomed = QPixmap::fromImage(
            crop.scaled(zoomRect.size() * zoomDpr, Qt::IgnoreAspectRatio,
                        Qt::FastTransformation));
        zoomed.setDevicePixelRatio(zoomDpr);
        p.drawPixmap(zoomRect.topLeft(), zoomed);

        // 十字线：穿过中心像素中线的单像素线，互补色保证可见
        const QPoint off = c - src.topLeft(); // 中心像素在源区域内的偏移
        const int cx = zoomRect.left() + off.x() * kZoom + kZoom / 2;
        const int cy = zoomRect.top() + off.y() * kZoom + kZoom / 2;
        const QColor xColor(255 - center.red(), 255 - center.green(), 255 - center.blue());
        p.setPen(QPen(xColor, 1));
        p.drawLine(zoomRect.left(), cy, zoomRect.right(), cy);
        p.drawLine(cx, zoomRect.top(), cx, zoomRect.bottom());
        p.restore();

        // ---- 下 1/3：颜色信息区（白底 + 圆角色块徽章 + Hex/RGB 文本） ----
        const QRect colorRect(cardRect.left(), zoomRect.bottom() + 1, kZoomW, kColorH);
        p.fillRect(colorRect, Qt::white);
        const QRect chip(colorRect.left() + 12, colorRect.top() + 12, 26, 26);
        QPainterPath chipPath;
        chipPath.addRoundedRect(QRectF(chip), 8, 8);
        p.fillPath(chipPath, center);
        p.setPen(QPen(QColor(0, 0, 0, 35), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(chip), 8, 8);

        const QRect textRect(chip.right() + 11, colorRect.top(),
                             colorRect.right() - chip.right() - 11, kColorH);
        QFont f = p.font();
        f.setPixelSize(14);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(0, 0, 0));
        p.drawText(textRect.adjusted(0, 8, 0, 0), Qt::AlignLeft | Qt::AlignTop,
                   center.name(QColor::HexRgb).toUpper());
        f.setPixelSize(11);
        f.setWeight(QFont::Normal);
        p.setFont(f);
        p.drawText(textRect.adjusted(0, 0, 0, -8), Qt::AlignLeft | Qt::AlignBottom,
                   QString("RGB(%1, %2, %3)").arg(center.red()).arg(center.green()).arg(center.blue()));

        // 卡片边框
        p.setPen(QPen(QColor(0, 0, 0, 80), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(cardRect), 12, 12);
    }

} // namespace poptools
