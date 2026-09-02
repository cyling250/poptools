#include "SettingsDialog.h"
#include "core/settings/Settings.h"
#include "core/translation/ApiTester.h"

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
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QToolButton>

namespace poptools {

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("PopTools 设置");
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
        "QLabel { color: #555; }"
        "QToolButton { background: #fff; border: 1px solid #D0D0D0; border-radius: 3px;"
        "  padding: 3px 8px; color: #1A1A1A; }"
        "QToolButton:hover { background: #E8F0FE; border-color: #2B6FF2; color: #2B6FF2; }");

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

    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItem(QStringLiteral("OpenAI 兼容（DeepSeek / Ollama 等）"),
                           QStringLiteral("openai"));
    m_formatCombo->addItem(QStringLiteral("Anthropic（Claude / DeepSeek 兼容端点）"),
                           QStringLiteral("anthropic"));
    // 切换格式时同步 Base URL 占位提示（Anthropic 端点挂在 …/anthropic 前缀下）
    connect(m_formatCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_urlEdit->setPlaceholderText(index == 1
            ? QStringLiteral("https://api.deepseek.com/anthropic")
            : QStringLiteral("https://api.deepseek.com/v1"));
    });
    m_formatCombo->setCurrentIndex(qMax(0, m_formatCombo->findData(Settings::instance().apiFormat())));
    apiForm->addRow("接口格式:", m_formatCombo);

    m_urlEdit = new QLineEdit(Settings::instance().apiBaseUrl(), this);
    m_urlEdit->setPlaceholderText("https://api.deepseek.com/v1");
    apiForm->addRow("Base URL:", m_urlEdit);
    m_keyEdit = new QLineEdit(Settings::instance().apiKey(), this);
    m_keyEdit->setEchoMode(QLineEdit::Password);
    m_keyEdit->setPlaceholderText("sk-xxxxxxxxxxxxxxxx");
    // 密码框看不见内容、粘贴错 Key 无法察觉（401 报错难排查），提供显示切换
    m_keyToggle = new QToolButton(this);
    m_keyToggle->setText(QStringLiteral("显示"));
    m_keyToggle->setToolTip(QStringLiteral("显示/隐藏 API Key，便于核对是否粘贴正确"));
    connect(m_keyToggle, &QToolButton::clicked, this, [this] {
        const bool show = m_keyEdit->echoMode() == QLineEdit::Password;
        m_keyEdit->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
        m_keyToggle->setText(show ? QStringLiteral("隐藏") : QStringLiteral("显示"));
    });
    auto *keyRow = new QHBoxLayout;
    keyRow->setContentsMargins(0, 0, 0, 0);
    keyRow->setSpacing(6);
    keyRow->addWidget(m_keyEdit, 1);
    keyRow->addWidget(m_keyToggle);
    apiForm->addRow("API Key:", keyRow);
    m_modelEdit = new QLineEdit(Settings::instance().apiModel(), this);
    m_modelEdit->setPlaceholderText("deepseek-chat");
    apiForm->addRow("Model:", m_modelEdit);
    mainLayout->addLayout(apiForm);

    // 测试连接：用表单当前值（未保存前也能测）按所选接口格式发一次真实请求，结果就地显示
    m_testBtn = new QPushButton(QStringLiteral("测试连接"), this);
    m_testBtn->setToolTip(QStringLiteral("用上方填写的接口格式 / Base URL / API Key / Model 发起一次真实翻译请求"));
    m_testStatus = new QLabel(this);
    m_tester = new ApiTester(this);
    connect(m_tester, &ApiTester::finished, this, [this](bool ok, const QString &message) {
        m_testBtn->setEnabled(true);
        m_testBtn->setText(QStringLiteral("测试连接"));
        m_testStatus->setText(ok ? QStringLiteral("✓ ") + message
                                 : QStringLiteral("✗ ") + message);
        m_testStatus->setStyleSheet(ok ? "QLabel { color: #1E8E3E; }"
                                       : "QLabel { color: #D93025; }");
    });
    connect(m_testBtn, &QPushButton::clicked, this, [this] {
        if (m_tester->busy())
            return;
        m_testBtn->setEnabled(false);
        m_testBtn->setText(QStringLiteral("测试中…"));
        m_testStatus->setText(QString());
        m_tester->test(m_urlEdit->text(), m_keyEdit->text(), m_modelEdit->text(),
                       apiFormatFromSetting(m_formatCombo->currentData().toString()));
    });
    auto *testRow = new QHBoxLayout;
    testRow->setContentsMargins(0, 4, 0, 0);
    testRow->setSpacing(8);
    testRow->addWidget(m_testBtn);
    testRow->addWidget(m_testStatus, 1);
    mainLayout->addLayout(testRow);

    addSeparator();

    // ---- 翻译领域与术语 ----
    addSection("翻译领域与术语");
    auto *domainForm = new QFormLayout;
    domainForm->setContentsMargins(0, 0, 0, 0);
    domainForm->setSpacing(6);
    domainForm->setLabelAlignment(Qt::AlignRight);

    // 领域：逗号/顿号分隔，注入提示词让模型按领域术语翻译
    m_domainEdit = new QLineEdit(Settings::instance().translateDomains().join(", "), this);
    m_domainEdit->setPlaceholderText("如：医学, 法律, 计算机（逗号分隔，可留空）");
    domainForm->addRow("领域:", m_domainEdit);

    // 专有名词翻译对：每行一条 "原文=译文"，整体注入提示词强制按术语表翻译
    m_termsEdit = new QPlainTextEdit(this);
    QString termLines;
    for (const auto &term : Settings::instance().translateTerms())
        termLines += term.first + "=" + term.second + "\n";
    m_termsEdit->setPlainText(termLines);
    m_termsEdit->setPlaceholderText(
        "每行一条：原文=译文\n如：\nworkflow=工作流\nmyocardial infarction=心肌梗死");
    m_termsEdit->setFixedHeight(88);
    m_termsEdit->setStyleSheet(
        "QPlainTextEdit { background: #fff; border: 1px solid #D0D0D0; border-radius: 3px;"
        "  padding: 4px 6px; color: #1A1A1A; }"
        "QPlainTextEdit:focus { border-color: #2B6FF2; }");
    domainForm->addRow("术语表:", m_termsEdit);
    mainLayout->addLayout(domainForm);

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
        s.setApiFormat(m_formatCombo->currentData().toString());
        s.setApiBaseUrl(m_urlEdit->text().trimmed());
        s.setApiKey(m_keyEdit->text().trimmed());
        s.setApiModel(m_modelEdit->text().trimmed());
        s.setTranslateDomains(parseDomains());
        s.setTranslateTerms(parseTerms());
        s.setPinSaveDir(m_dirEdit->text().trimmed());
    });
}

// 领域输入 → 列表：中英文逗号、顿号、分号均可作分隔符，空白项丢弃
QStringList SettingsDialog::parseDomains() const
{
    QString raw = m_domainEdit->text();
    raw.replace(QRegularExpression("[，、;；]"), ",");
    QStringList domains;
    for (const QString &part : raw.split(',', Qt::SkipEmptyParts)) {
        const QString d = part.trimmed();
        if (!d.isEmpty())
            domains << d;
    }
    return domains;
}

// 术语输入 → 翻译对：每行 "原文=译文"，按第一个 = 切分（译文可含 =）；
// 无 = 或任一侧为空的行跳过
QList<QPair<QString, QString>> SettingsDialog::parseTerms() const
{
    QList<QPair<QString, QString>> terms;
    const QStringList lines = m_termsEdit->toPlainText().split('\n');
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (t.isEmpty())
            continue;
        const int eq = t.indexOf('=');
        if (eq <= 0 || eq == t.size() - 1)
            continue;
        terms.append({t.left(eq).trimmed(), t.mid(eq + 1).trimmed()});
    }
    return terms;
}

// 按当前注册表状态刷新按钮文本：已加入 → 显示"取消开机启动"，未加入 → 显示"加入到开机启动"
void SettingsDialog::updateAutoStartButton()
{
    m_autoStart->setText(Settings::instance().autoStartEnabled()
                             ? "取消开机启动"
                             : "加入到开机启动");
}

} // namespace poptools
