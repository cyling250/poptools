#include "MainWindow.h"

#include "core/ocr/OcrService.h"
#include "core/settings/Settings.h"
#include "core/translation/Translator.h"
#include "ui/settings/SettingsDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QCursor>
#include <QMessageBox>
#include <QTimer>

namespace snipaste {

// ==================== MainWindow ====================

MainWindow::MainWindow(QObject *parent)
    : QObject(parent)
{
    // 托盘（使用资源图标：Material Design 相机图标，Apache 2.0 许可）
    m_tray = new QSystemTrayIcon(QIcon(":/app_icon.svg"), this);
    m_tray->setToolTip("MySnipaste\n按 F1 截图");

    m_menu = new QMenu();
    m_menu->setObjectName("TrayMenu");
    // 菜单标题显示当前模式（托盘区域文字提示）
    m_menu->setTitle("MySnipaste");
    m_menu->addAction("截图 (F1)", this, [this] {
        if (!m_capture->isVisible())
            m_capture->startCapture();
    });
    m_menu->addAction("设置...", this, &MainWindow::onShowSettings);
    m_menu->addSeparator();
    m_menu->addAction("关于", this, [this] {
        QMessageBox::about(m_result, "MySnipaste",
            "MySnipaste v0.1.0\n\n"
            "全场景截图软件：区域截图 + OCR 识别 + 翻译\n\n"
            "快捷键：F1 截图\n"
            "OCR 引擎：Tesseract 5（本地离线）\n"
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
    m_ocr = new OcrService(this);
    connect(m_ocr, &OcrService::ocrFinished, this, &MainWindow::onOcrFinished);
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

    // 全局热键：F1 截图 / F3 贴图 / Ctrl+X 划词翻译
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
            "未找到 OCR 引擎（libtesseract-5.dll），请确认其位于程序目录的 tesseract 文件夹中。");
        return;
    }
    m_ocr->recognize(m_lastPixmap.toImage());
}

void MainWindow::onTranslateRequested(const QString &text)
{
    if (text.trimmed().isEmpty())
        return;
    // 旧结果面板入口（当前不再自动弹出）：自动识别源语言，目标语言固定中文
    m_translator->translate(text, QStringLiteral("auto"), QStringLiteral("zh"));
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
    m_selSourceText = text;
    if (m_translator->busy()) {
        // 翻译器正忙（例如 OCR 结果面板翻译中），放弃本次划词翻译
        m_selSourceText.clear();
        return;
    }

    // 弹出卡片，显示原文 + 语言控件，等待用户点击翻译
    auto *card = new TranslateCard();
    card->setSourceText(text);
    card->show();
    m_selTranslateCard = card;

    // 连接翻译请求：用户点击卡片内的翻译按钮时触发
    connect(card, &TranslateCard::translateRequested, this,
            [this, card](const QString &text, const QString &fromLang, const QString &toLang) {
                m_selSourceText = text;
                m_selTranslateCard = card;
                m_translator->translate(text, fromLang, toLang, false);
            });

    // 卡片关闭时清理状态
    connect(card, &TranslateCard::closed, this, [this]() {
        m_selSourceText.clear();
        m_selTranslateCard.clear();
    });
}

void MainWindow::onSelectionTranslateError(const QString &error)
{
    // 如果卡片已弹出但获取文本失败，在卡片中显示错误
    if (m_selTranslateCard) {
        m_selTranslateCard->showError(error);
        m_selSourceText.clear();
        m_selTranslateCard.clear();
    }
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
    // 划词翻译：用 TranslateCard 展示，不影响现有 ResultDialog 流程
    if (!m_selSourceText.isEmpty()) {
        if (m_selTranslateCard) {
            if (ok)
                m_selTranslateCard->setTranslateResult(result);
            else
                m_selTranslateCard->showError(error);
        }
        m_selSourceText.clear();
        m_selTranslateCard.clear();
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
