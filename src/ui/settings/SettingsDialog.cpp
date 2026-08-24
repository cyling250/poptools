#include "SettingsDialog.h"
#include "core/settings/Settings.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QFrame>
#include <QLabel>

namespace snipaste {

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Snipaste 设置");
    setMinimumWidth(460);

    // 对话框整体样式
    setStyleSheet(
        "QDialog { background: #F5F5F5; }"
        "QGroupBox { font-weight: bold; border: none; margin-top: 8px; }"
        "QLineEdit { background: #fff; border: 1px solid #D0D0D0; border-radius: 3px;"
        "  padding: 4px 6px; color: #1A1A1A; }"
        "QLineEdit:focus { border-color: #2B6FF2; }"
        "QPushButton { background: #fff; border: 1px solid #D0D0D0; border-radius: 3px;"
        "  padding: 4px 12px; color: #1A1A1A; }"
        "QPushButton:hover { background: #E8F0FE; border-color: #2B6FF2; color: #2B6FF2; }"
        "QCheckBox { color: #1A1A1A; }"
        "QLabel { color: #555; }");

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    auto addSection = [&](const QString &title) {
        auto *label = new QLabel(title, this);
        label->setStyleSheet("QLabel { font-weight: bold; color: #1A1A1A; font-size: 13px;"
                             "  padding: 8px 0 4px 0; }");
        mainLayout->addWidget(label);
    };

    auto addSeparator = [&]() {
        auto *sep = new QFrame(this);
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet("QFrame { color: #E0E0E0; margin: 8px 0; }");
        mainLayout->addWidget(sep);
    };

    // ---- 通用 ----
    addSection("通用");
    m_autoStart = new QPushButton(this);
    updateAutoStartButton();
    connect(m_autoStart, &QPushButton::clicked, this, [this] {
        Settings &s = Settings::instance();
        s.setAutoStartEnabled(!s.autoStartEnabled());
        updateAutoStartButton();
    });
    auto *autoStartRow = new QHBoxLayout;
    autoStartRow->addWidget(m_autoStart);
    autoStartRow->addStretch();
    mainLayout->addLayout(autoStartRow);

    addSeparator();

    // ---- 翻译 API ----
    addSection("翻译 API");
    auto *apiForm = new QFormLayout;
    apiForm->setContentsMargins(0, 0, 0, 0);
    apiForm->setSpacing(6);
    apiForm->setLabelAlignment(Qt::AlignRight);

    m_urlEdit = new QLineEdit(Settings::instance().apiBaseUrl(), this);
    m_urlEdit->setPlaceholderText("https://api.deepseek.com/v1");
    apiForm->addRow("Base URL:", m_urlEdit);
    m_keyEdit = new QLineEdit(Settings::instance().apiKey(), this);
    m_keyEdit->setEchoMode(QLineEdit::Password);
    m_keyEdit->setPlaceholderText("sk-xxxxxxxxxxxxxxxx");
    apiForm->addRow("API Key:", m_keyEdit);
    m_modelEdit = new QLineEdit(Settings::instance().apiModel(), this);
    m_modelEdit->setPlaceholderText("deepseek-chat");
    apiForm->addRow("Model:", m_modelEdit);
    mainLayout->addLayout(apiForm);

    addSeparator();

    // ---- 截图与贴图 ----
    addSection("截图与贴图");
    auto *captureForm = new QFormLayout;
    captureForm->setContentsMargins(0, 0, 0, 0);
    captureForm->setSpacing(6);
    captureForm->setLabelAlignment(Qt::AlignRight);

    auto *dirRow = new QHBoxLayout;
    m_dirEdit = new QLineEdit(Settings::instance().pinSaveDir(), this);
    m_dirEdit->setPlaceholderText("留空不自动保存");
    dirRow->addWidget(m_dirEdit, 1);
    auto *browseBtn = new QPushButton("浏览...", this);
    dirRow->addWidget(browseBtn);
    connect(browseBtn, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, "选择贴图默认保存文件夹", m_dirEdit->text().trimmed());
        if (!dir.isEmpty())
            m_dirEdit->setText(dir);
    });
    captureForm->addRow("贴图保存:", dirRow);

    m_smartImageCheck = new QCheckBox(
        "开启后，智能框选叠加图像边缘识别（可能含文字笔画噪声）", this);
    m_smartImageCheck->setChecked(Settings::instance().smartImageRecognitionEnabled());
    captureForm->addRow("框选图像识别:", m_smartImageCheck);
    mainLayout->addLayout(captureForm);

    mainLayout->addStretch();

    // ---- 底部按钮 ----
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto *okBtn = buttons->button(QDialogButtonBox::Ok);
    if (okBtn)
        okBtn->setStyleSheet(
            "QPushButton { background: #2B6FF2; color: #fff; border: 1px solid #2B6FF2;"
            "  border-radius: 3px; padding: 5px 16px; min-width: 70px; }"
            "QPushButton:hover { background: #1A5CDB; }");
    auto *cancelBtn = buttons->button(QDialogButtonBox::Cancel);
    if (cancelBtn)
        cancelBtn->setStyleSheet(
            "QPushButton { background: #fff; color: #1A1A1A; border: 1px solid #D0D0D0;"
            "  border-radius: 3px; padding: 5px 16px; min-width: 70px; }"
            "QPushButton:hover { background: #E8F0FE; border-color: #2B6FF2; color: #2B6FF2; }");
    mainLayout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // 点击 OK 时保存设置
    connect(this, &QDialog::accepted, this, [this] {
        Settings &s = Settings::instance();
        s.setApiBaseUrl(m_urlEdit->text().trimmed());
        s.setApiKey(m_keyEdit->text().trimmed());
        s.setApiModel(m_modelEdit->text().trimmed());
        s.setPinSaveDir(m_dirEdit->text().trimmed());
        s.setSmartImageRecognitionEnabled(m_smartImageCheck->isChecked());
    });
}

// 按当前注册表状态刷新按钮文本：已加入 → 显示"取消开机启动"，未加入 → 显示"加入到开机启动"
void SettingsDialog::updateAutoStartButton()
{
    m_autoStart->setText(Settings::instance().autoStartEnabled()
                             ? "取消开机启动"
                             : "加入到开机启动");
}

} // namespace snipaste
