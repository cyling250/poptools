#include "MainWindow.h"
#include "Settings.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QGroupBox>
#include <QTextEdit>
#include <QLineEdit>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QMessageBox>
#include <QPainter>

#include <windows.h>

// ==================== ResultDialog ====================

ResultDialog::ResultDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("MySnipaste - 截图结果");
    setMinimumSize(560, 480);

    auto *layout = new QVBoxLayout(this);

    // 截图预览
    m_imageLabel = new QLabel(this);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setMinimumHeight(160);
    layout->addWidget(m_imageLabel);

    // OCR 文本
    auto *ocrGroup = new QGroupBox("OCR 识别文本", this);
    auto *ocrLayout = new QVBoxLayout(ocrGroup);
    m_ocrLabel = new QLabel("（尚未识别）", ocrGroup);
    m_ocrLabel->setWordWrap(true);
    m_ocrLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ocrLayout->addWidget(m_ocrLabel);
    layout->addWidget(ocrGroup);

    // 翻译结果
    auto *trGroup = new QGroupBox("翻译结果", this);
    auto *trLayout = new QVBoxLayout(trGroup);
    m_translateLabel = new QLabel("（尚未翻译）", trGroup);
    m_translateLabel->setWordWrap(true);
    m_translateLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    trLayout->addWidget(m_translateLabel);
    layout->addWidget(trGroup);

    // 按钮行
    auto *btnLayout = new QHBoxLayout;
    m_ocrBtn = new QPushButton("识别文字 (OCR)", this);
    m_translateBtn = new QPushButton("翻译", this);
    m_translateBtn->setEnabled(false);
    auto *copyBtn = new QPushButton("复制全部", this);
    auto *closeBtn = new QPushButton("关闭", this);
    btnLayout->addWidget(m_ocrBtn);
    btnLayout->addWidget(m_translateBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(copyBtn);
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);

    connect(m_ocrBtn, &QPushButton::clicked, this, &ResultDialog::onOcrClicked);
    connect(m_translateBtn, &QPushButton::clicked, this, &ResultDialog::onTranslateClicked);
    connect(copyBtn, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_ocrText + "\n\n" + m_translateLabel->text());
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void ResultDialog::setImage(const QPixmap &pix)
{
    m_imageLabel->setPixmap(pix.scaled(m_imageLabel->size(), Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
}

void ResultDialog::setOcrText(const QString &text)
{
    m_ocrText = text;
    m_ocrLabel->setText(text.isEmpty() ? "（识别失败或无文本）" : text);
    m_translateBtn->setEnabled(!text.isEmpty());
}

void ResultDialog::setTranslateText(const QString &text)
{
    m_translateLabel->setText(text.isEmpty() ? "（尚未翻译）" : text);
}

void ResultDialog::onOcrClicked()
{
    emit ocrRequested();
}

void ResultDialog::onTranslateClicked()
{
    emit translateRequested(m_ocrText);
}

// ==================== MainWindow ====================

MainWindow::MainWindow(QObject *parent)
    : QObject(parent)
{
    qApp->installNativeEventFilter(this);

    // 托盘（运行时绘制图标，无需二进制资源）
    QPixmap iconPix(32, 32);
    iconPix.fill(Qt::transparent);
    {
        QPainter p(&iconPix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 150, 255));
        p.drawRoundedRect(3, 6, 26, 20, 3, 3);       // 相机机身
        p.setBrush(QColor(0, 120, 220));
        p.drawRoundedRect(10, 1, 12, 6, 2, 2);        // 取景器
        p.setBrush(QColor(255, 255, 255));
        p.drawEllipse(10, 12, 12, 12);                // 镜头外圈
        p.setBrush(QColor(0, 150, 255));
        p.drawEllipse(13, 15, 6, 6);                  // 镜头内圈
    }

    m_tray = new QSystemTrayIcon(QIcon(iconPix), this);
    m_tray->setToolTip("MySnipaste\n按 F1 截图");

    m_menu = new QMenu();
    m_menu->addAction("截图 (F1)", this, [this] {
        if (!m_capture->isVisible())
            m_capture->startCapture();
    });
    m_menu->addAction("设置...", this, &MainWindow::onShowSettings);
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

    registerHotKeys();
}

MainWindow::~MainWindow()
{
    if (m_hotkeyRegistered)
        UnregisterHotKey(nullptr, m_hotkeyId);
}

bool MainWindow::registerHotKeys()
{
    const quint32 vk = Settings::instance().captureHotKeyVk();
    m_hotkeyRegistered = RegisterHotKey(nullptr, m_hotkeyId, 0, vk) != 0;
    return m_hotkeyRegistered;
}

bool MainWindow::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType)
    Q_UNUSED(result)

    auto *msg = static_cast<MSG *>(message);
    if (msg->message == WM_HOTKEY && msg->wParam == m_hotkeyId) {
        if (!m_capture->isVisible())
            m_capture->startCapture();
        return true;
    }
    return false;
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
    QDialog dlg(m_result);
    dlg.setWindowTitle("MySnipaste 设置");
    auto *form = new QFormLayout(&dlg);

    // Tesseract 路径
    auto *tessEdit = new QLineEdit(Settings::instance().tesseractPath(), &dlg);
    form->addRow("Tesseract 路径:", tessEdit);

    // 翻译模式
    auto *modeCombo = new QComboBox(&dlg);
    modeCombo->addItem("大模型 API", "api");
    modeCombo->addItem("本地引擎", "local");
    int idx = modeCombo->findData(Settings::instance().translateMode());
    modeCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    form->addRow("翻译模式:", modeCombo);

    // API 配置
    auto *urlEdit = new QLineEdit(Settings::instance().apiBaseUrl(), &dlg);
    form->addRow("API Base URL:", urlEdit);
    auto *keyEdit = new QLineEdit(Settings::instance().apiKey(), &dlg);
    keyEdit->setEchoMode(QLineEdit::Password);
    form->addRow("API Key:", keyEdit);
    auto *modelEdit = new QLineEdit(Settings::instance().apiModel(), &dlg);
    form->addRow("Model:", modelEdit);
    auto *langEdit = new QLineEdit(Settings::instance().targetLang(), &dlg);
    form->addRow("目标语言 (zh/en...):", langEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        Settings &s = Settings::instance();
        s.setTesseractPath(tessEdit->text().trimmed());
        s.setTranslateMode(modeCombo->currentData().toString());
        s.setApiBaseUrl(urlEdit->text().trimmed());
        s.setApiKey(keyEdit->text().trimmed());
        s.setApiModel(modelEdit->text().trimmed());
        s.setTargetLang(langEdit->text().trimmed());
    }
}
