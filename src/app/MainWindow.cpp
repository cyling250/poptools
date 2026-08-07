#include "MainWindow.h"

#include "core/ocr/OcrService.h"
#include "core/settings/Settings.h"
#include "core/translation/Translator.h"
#include "ui/settings/SettingsDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QMessageBox>

namespace snipaste {

// ==================== MainWindow ====================

MainWindow::MainWindow(QObject *parent)
    : QObject(parent)
{
    // 托盘（使用资源图标：Material Design 相机图标，Apache 2.0 许可）
    m_tray = new QSystemTrayIcon(QIcon(":/app_icon.svg"), this);
    m_tray->setToolTip("MySnipaste\n按 F1 截图");

    m_menu = new QMenu();
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

    // 截图窗口
    m_capture = new CaptureWindow();
    connect(m_capture, &CaptureWindow::captured, this, &MainWindow::onCaptured);

    // OCR 与翻译
    m_ocr = new OcrService(this);
    connect(m_ocr, &OcrService::ocrFinished, this, &MainWindow::onOcrFinished);
    m_translator = new Translator(this);
    connect(m_translator, &Translator::translateFinished,
            this, &MainWindow::onTranslateFinished);

    // 结果面板
    m_result = new ResultDialog();
    connect(m_result, &ResultDialog::ocrRequested, this, &MainWindow::onOcrRequested);
    connect(m_result, &ResultDialog::translateRequested,
            this, &MainWindow::onTranslateRequested);

    // 全局热键：F1 截图 / F3 贴图（贴图功能架构预留）
    const quint32 vk = Settings::instance().captureHotKeyVk();
    m_hotKey.registerKey(vk, kCaptureHotKeyId);
    m_hotKey.registerKey(0x72 /* VK_F3 */, kPinHotKeyId);
    connect(&m_hotKey, &GlobalHotKey::activated, this, [this](int id) {
        if (id == kCaptureHotKeyId) {
            if (!m_capture->isVisible())
                m_capture->startCapture();
        } else if (id == kPinHotKeyId) {
            // TODO(PinWindow): 将 m_lastPixmap 交给 PinWindow::pin() 置顶显示
        }
    });
}

void MainWindow::onCaptured(const QPixmap &pixmap)
{
    if (pixmap.isNull())
        return; // 用户取消

    m_lastPixmap = pixmap;
    QGuiApplication::clipboard()->setPixmap(pixmap);

    m_result->setImage(pixmap);
    m_result->setOcrText("");
    m_result->setTranslateText("");
    m_result->show();
    m_result->raise();
    m_result->activateWindow();
}

void MainWindow::onOcrRequested()
{
    if (m_lastPixmap.isNull())
        return;
    if (!m_ocr->available()) {
        QMessageBox::warning(m_result, "OCR",
            "未找到 tesseract.exe，请在设置中配置 Tesseract 路径。");
        return;
    }
    m_ocr->recognize(m_lastPixmap.toImage());
}

void MainWindow::onTranslateRequested(const QString &text)
{
    if (text.trimmed().isEmpty())
        return;
    m_translator->translate(text);
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
    if (ok)
        m_result->setTranslateText(result);
    else
        QMessageBox::warning(m_result, "翻译失败", error);
}

void MainWindow::onShowSettings()
{
    SettingsDialog dlg(m_result);
    dlg.exec();
}

} // namespace snipaste
