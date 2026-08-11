#include "SettingsDialog.h"
#include "core/settings/Settings.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QDialogButtonBox>

namespace snipaste {

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("MySnipaste 设置");
    setMinimumWidth(420);
    auto *form = new QFormLayout(this);

    // 开机自启：点击切换加入/取消（按钮文本随当前状态变化）
    m_autoStart = new QPushButton(this);
    updateAutoStartButton();
    connect(m_autoStart, &QPushButton::clicked, this, [this] {
        Settings &s = Settings::instance();
        s.setAutoStartEnabled(!s.autoStartEnabled());
        updateAutoStartButton();
    });
    form->addRow("开机启动:", m_autoStart);

    // API 配置（翻译仅支持大模型，DeepSeek 官方 API）
    m_urlEdit = new QLineEdit(Settings::instance().apiBaseUrl(), this);
    form->addRow("API Base URL:", m_urlEdit);
    m_keyEdit = new QLineEdit(Settings::instance().apiKey(), this);
    m_keyEdit->setEchoMode(QLineEdit::Password);
    form->addRow("API Key:", m_keyEdit);
    m_modelEdit = new QLineEdit(Settings::instance().apiModel(), this);
    form->addRow("Model:", m_modelEdit);

    // 贴图默认保存文件夹（F3 张贴时自动保存到此目录）
    auto *dirRow = new QHBoxLayout;
    m_dirEdit = new QLineEdit(Settings::instance().pinSaveDir(), this);
    dirRow->addWidget(m_dirEdit, 1);
    auto *browseBtn = new QPushButton("浏览...", this);
    dirRow->addWidget(browseBtn);
    connect(browseBtn, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, "选择贴图默认保存文件夹", m_dirEdit->text().trimmed());
        if (!dir.isEmpty())
            m_dirEdit->setText(dir);
    });
    form->addRow("贴图保存文件夹:", dirRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    form->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // 点击 OK 时保存设置
    connect(this, &QDialog::accepted, this, [this] {
        Settings &s = Settings::instance();
        s.setApiBaseUrl(m_urlEdit->text().trimmed());
        s.setApiKey(m_keyEdit->text().trimmed());
        s.setApiModel(m_modelEdit->text().trimmed());
        s.setPinSaveDir(m_dirEdit->text().trimmed());
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
