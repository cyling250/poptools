#include "MainWindow.h"

#include "core/ocr/PaddleOcrService.h"
#include "core/settings/Settings.h"
#include "core/translation/Translator.h"
#include "ui/settings/SettingsDialog.h"

#ifdef Q_OS_WIN
#include <windows.h> // 全局鼠标钩子回调中的 WindowFromPoint / GetWindowThreadProcessId
#endif

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QCursor>
#include <QEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QTimer>

namespace snipaste {

// ==================== MainWindow ====================

MainWindow::MainWindow(QObject *parent)
    : QObject(parent)
{
    // 托盘（使用资源图标：Material Design 相机图标，Apache 2.0 许可）
    m_tray = new QSystemTrayIcon(QIcon(":/app_icon.svg"), this);
    m_tray->setToolTip("Snipaste\n按 F1 截图");

    m_menu = new QMenu();
    m_menu->setObjectName("TrayMenu");
    // 菜单标题显示当前模式（托盘区域文字提示）
    m_menu->setTitle("Snipaste");
    m_menu->addAction("截图 (F1)", this, [this] {
        if (!m_capture->isVisible())
            m_capture->startCapture();
    });
    m_menu->addAction("设置...", this, &MainWindow::onShowSettings);
    m_menu->addSeparator();
    m_menu->addAction("关于", this, [this] {
        QMessageBox::about(m_result, "Snipaste",
            "Snipaste v0.1.3\n\n"
            "全场景截图软件：区域截图 + OCR 识别 + 翻译\n\n"
            "快捷键：F1 截图\n"
            "OCR 引擎：PaddleOCR（本地离线）\n"
            "翻译：OpenAI 兼容大模型 API（可配置）");
    });
    m_menu->addSeparator();
    m_menu->addAction("退出", this, &QApplication::quit);
    m_tray->setContextMenu(m_menu);
    m_tray->show();

    // 截图窗口（无父窗口，顶层窗口由 QApplication 在退出时清理）
    m_capture = new CaptureWindow();
    connect(m_capture, &CaptureWindow::captured, this, &MainWindow::onCaptured);

    // OCR 与翻译
    m_ocr = new PaddleOcrService(this);
    connect(m_ocr, &PaddleOcrService::ocrFinished, this, &MainWindow::onOcrFinished);
    m_translator = new Translator(this);
    connect(m_translator, &Translator::translateFinished,
            this, &MainWindow::onTranslateFinished);

    // 划词翻译（获取选中文本）
    m_selTranslator = new SelectionTranslator(this);
    connect(m_selTranslator, &SelectionTranslator::textAcquired,
            this, &MainWindow::onSelectionTranslateText);
    connect(m_selTranslator, &SelectionTranslator::errorOccurred,
            this, &MainWindow::onSelectionTranslateError);

    // 结果面板（无父窗口，顶层窗口由 QApplication 在退出时清理）
    m_result = new ResultDialog();
    connect(m_result, &ResultDialog::ocrRequested, this, &MainWindow::onOcrRequested);
    connect(m_result, &ResultDialog::translateRequested,
            this, &MainWindow::onTranslateRequested);

    // 全局热键：F1 截图 / F3 贴图 / Ctrl+Alt+Shift+X 划词翻译
    const quint32 vk = Settings::instance().captureHotKeyVk();
    m_hotKey.registerKey(vk, kCaptureHotKeyId);
    m_hotKey.registerKey(0x72 /* VK_F3 */, kPinHotKeyId);
    m_hotKey.registerKey(
        Settings::instance().selectionTranslateHotKeyVk(),
        kSelectionTranslateHotKeyId,
        Settings::instance().selectionTranslateHotKeyModifiers());
    connect(&m_hotKey, &GlobalHotKey::activated, this, [this](int id) {
        if (id == kCaptureHotKeyId) {
            if (!m_capture->isVisible())
                m_capture->startCapture();
        } else if (id == kPinHotKeyId) {
            // 热键在原生事件过滤器中触发：UI 操作延迟到事件循环，避免窗口创建/激活时序问题
            QTimer::singleShot(0, this, [this] { onPinHotKey(); });
        } else if (id == kSelectionTranslateHotKeyId) {
            QTimer::singleShot(0, this, [this] { onSelectionTranslateHotKey(); });
        }
    });

    // 应用级事件过滤器：划词卡片显示时点击本应用内卡片外部 → 隐藏卡片
    qApp->installEventFilter(this);

    // 全局鼠标钩子：划词卡片不抢焦点，点击"其他应用"的窗口时 Qt 收不到任何事件，
    // 需系统级钩子感知（本应用内的点击仍由上面的应用级事件过滤器处理）
    m_mouseHook = new GlobalMouseHook(this);
    connect(m_mouseHook, &GlobalMouseHook::mousePressed, this, [this](const QPoint &nativePos) {
        if (!m_selTranslateCard || !m_selTranslateCard->isVisible())
            return;
        // 落点窗口属于其他进程 → 用户点击了卡片外部（本应用外的窗口），隐藏卡片。
        // nativePos 为物理像素，直接与 Win32 窗口归属比较，不涉及 Qt 逻辑坐标换算
        const POINT pt{nativePos.x(), nativePos.y()};
        if (HWND hwnd = WindowFromPoint(pt)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != GetCurrentProcessId())
                hideSelectionCard();
        }
    });
}

void MainWindow::onCaptured(const QPixmap &pixmap, const QRect &selectionLogical)
{
    if (pixmap.isNull())
        return; // 用户取消

    // 记录最近截图与选区位置（F3 贴图定位用）；确认时截图已复制到剪贴板，
    // 且不再自动弹出结果面板（该面板已不使用）
    m_lastPixmap = pixmap;
    m_lastRect = selectionLogical;
}

void MainWindow::onPinHotKey()
{
    // 截图模式：先把当前截图复制到剪贴板并关闭截图模式（作为"剪贴板最新内容"），
    // 同时返回本次选区位置（截图所在位置即张贴区域）
    QRect captureSel;
    if (m_capture->isVisible())
        captureSel = m_capture->copyToClipboardAndClose();
    // 从剪贴板取最新图片张贴；剪贴板为文字时 pixmap 为空 → 无响应
    const QPixmap img = QGuiApplication::clipboard()->pixmap();
    if (img.isNull())
        return;

    // 张贴位置：本次截图位置 > 最近一次确认截图位置 > 鼠标当前位置（作为左上角点）
    QPoint pos;
    if (!captureSel.isEmpty())
        pos = captureSel.topLeft();
    else if (!m_lastRect.isEmpty())
        pos = m_lastRect.topLeft();
    else
        pos = QCursor::pos();

    // 多张贴图并存：每次 F3 创建新实例，互不影响；关闭时（WA_DeleteOnClose）自动销毁
    auto *pin = new PinWindow();
    pin->pin(img, pos);
}

void MainWindow::onOcrRequested()
{
    if (m_lastPixmap.isNull())
        return;
    if (!m_ocr->available()) {
        QMessageBox::warning(m_result, "OCR",
            "未找到 OCR 引擎（PaddleOCR-json.exe），请确认其位于程序目录的 paddleocr 文件夹中。");
        return;
    }
    m_ocr->recognize(m_lastPixmap.toImage());
}

void MainWindow::onTranslateRequested(const QString &text)
{
    if (text.trimmed().isEmpty())
        return;
    // 旧结果面板入口（当前不再自动弹出）：自动识别源语言，目标语言用记忆值
    m_translator->translate(text, QStringLiteral("auto"),
                            Settings::instance().translateTargetLang());
}

void MainWindow::onSelectionTranslateHotKey()
{
    // 截图中不触发划词翻译
    if (m_capture->isVisible())
        return;
    m_selTranslator->trigger();
}

void MainWindow::onSelectionTranslateText(const QString &text)
{
    // 翻译器正忙（上一次翻译尚未返回）时放弃本次划词翻译
    if (m_translator->busy())
        return;

    // 全局唯一卡片：显示中则先隐藏（hideEvent 同步清空旧内容），
    // 再载入新原文显示在鼠标位置
    TranslateCard *card = ensureSelectionCard();
    card->hide();
    card->setSourceText(text); // 填原文 + 内容自适应 + 定位到鼠标附近
    card->show();
    m_mouseHook->start();      // 显示期间感知点击其他应用窗口（卡片外部）
    card->translateNow();      // 立即执行翻译（经 translateRequested 登记在途状态）
}

void MainWindow::onSelectionTranslateError(const QString &error)
{
    // 全局唯一卡片：显示获取选中文本失败的错误信息
    TranslateCard *card = ensureSelectionCard();
    card->hide();
    card->showError(error);
    card->moveNearCursor();
    card->show();
    m_mouseHook->start();
}

TranslateCard *MainWindow::ensureSelectionCard()
{
    if (!m_selTranslateCard) {
        // 全局唯一实例，随 QApplication 退出清理
        m_selTranslateCard = new TranslateCard();
        // 翻译请求：用户点击卡片内翻译按钮或新划词触发
        connect(m_selTranslateCard, &TranslateCard::translateRequested, this,
                [this](const QString &text, const QString &fromLang, const QString &toLang) {
                    m_selTranslateActive = true;
                    m_translator->translate(text, fromLang, toLang, false);
                });
        // 用户主动关闭（关闭按钮 / Esc）：走统一隐藏路径清理状态
        connect(m_selTranslateCard, &TranslateCard::closed, this, [this] {
            hideSelectionCard();
        });
    }
    return m_selTranslateCard;
}

void MainWindow::hideSelectionCard()
{
    if (m_selTranslateCard)
        m_selTranslateCard->hide(); // 内容清空在 hideEvent 中统一处理
    m_selTranslateActive = false;
    m_mouseHook->stop(); // 卡片隐藏后不再需要感知全局点击
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 划词卡片显示时点击卡片外部 → 隐藏卡片（内容清空由 hideEvent 统一处理）。
    // 卡片内部的顶层弹层（如下拉语言列表的容器窗口）不算"外部"。
    if (event->type() == QEvent::MouseButtonPress && m_selTranslateCard
        && m_selTranslateCard->isVisible()) {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && !m_selTranslateCard->isAncestorOf(widget)) {
            const QPoint globalPos =
                static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
            if (!m_selTranslateCard->frameGeometry().contains(globalPos))
                hideSelectionCard();
        }
    }
    return QObject::eventFilter(watched, event);
}

void MainWindow::onOcrFinished(bool ok, const QString &text, const QString &error)
{
    if (ok)
        m_result->setOcrText(text);
    else
        m_result->setOcrText(""); // 失败时不启用翻译按钮
}

void MainWindow::onTranslateFinished(bool ok, const QString &result, const QString &error)
{
    // 划词翻译卡片流程：结果回填唯一卡片。卡片已被用户隐藏/关闭时丢弃结果，
    // 避免误入结果面板路径（弹出失败提示框等）
    if (m_selTranslateActive) {
        m_selTranslateActive = false;
        if (m_selTranslateCard && m_selTranslateCard->isVisible()) {
            if (ok)
                m_selTranslateCard->setTranslateResult(result);
            else
                m_selTranslateCard->showError(error);
        }
        return;
    }

    // 原有流程：结果面板翻译
    if (ok)
        m_result->setTranslateText(result);
    else
        QMessageBox::warning(m_result, "翻译失败", error);
}

void MainWindow::onShowSettings()
{
    SettingsDialog dlg(m_result);
    dlg.exec();
    // 设置保存后刷新运行时组件：Translator 每次 translate() 读取最新 Settings，
    // 无需额外操作；若有缓存配置的组件，在此处刷新。
}

} // namespace snipaste
