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

    // 翻译模式
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem("大模型 API", "api");
    m_modeCombo->addItem("本地引擎", "local");
    int idx = m_modeCombo->findData(Settings::instance().translateMode());
    m_modeCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    form->addRow("翻译模式:", m_modeCombo);

    // API 配置
    m_urlEdit = new QLineEdit(Settings::instance().apiBaseUrl(), this);
    form->addRow("API Base URL:", m_urlEdit);
    m_keyEdit = new QLineEdit(Settings::instance().apiKey(), this);
    m_keyEdit->setEchoMode(QLineEdit::Password);
    form->addRow("API Key:", m_keyEdit);
    m_modelEdit = new QLineEdit(Settings::instance().apiModel(), this);
    form->addRow("Model:", m_modelEdit);
    m_langEdit = new QLineEdit(Settings::instance().targetLang(), this);
    form->addRow("目标语言 (zh/en...):", m_langEdit);

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
        s.setTranslateMode(m_modeCombo->currentData().toString());
        s.setApiBaseUrl(m_urlEdit->text().trimmed());
        s.setApiKey(m_keyEdit->text().trimmed());
        s.setApiModel(m_modelEdit->text().trimmed());
        s.setTargetLang(m_langEdit->text().trimmed());
        s.setPinSaveDir(m_dirEdit->text().trimmed());
    });
}

} // namespace snipaste
