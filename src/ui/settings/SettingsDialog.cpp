#include "SettingsDialog.h"
#include "core/settings/Settings.h"
#include "core/translation/ApiTester.h"
#include "ui/ThemeColors.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QRegularExpression>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyleHints>
#include <QToolButton>
#include <QVBoxLayout>

namespace poptools {

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("PopTools 设置");
    setMinimumSize(720, 520);

    // 主题化（跟随系统明暗，机制见 TranslateCard 同款）：P-022——
    // 自定义类一律 objectName ID 选择器；Qt 内置类用类型选择器
    setObjectName("settingsDialog");
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, &SettingsDialog::applyTheme);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ---- 内容区：左导航 + 右页面 ----
    auto *content = new QHBoxLayout;
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(0);

    m_nav = new QListWidget(this);
    m_nav->setObjectName("settingsNav");
    m_nav->setFixedWidth(150);
    m_nav->setFrameShape(QFrame::NoFrame);
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->addItem(QStringLiteral("通用"));
    m_nav->addItem(QStringLiteral("截图与贴图"));
    m_nav->addItem(QStringLiteral("翻译"));
    m_nav->addItem(QStringLiteral("活动监控"));
    m_nav->addItem(QStringLiteral("Goody 管家"));

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName("settingsPages");
    m_pages->addWidget(buildGeneralPage());
    m_pages->addWidget(buildCapturePage());
    m_pages->addWidget(buildTranslatePage());
    m_pages->addWidget(buildMonitorPage());
    m_pages->addWidget(buildGoodyPage());

    connect(m_nav, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_nav->setCurrentRow(0);

    content->addWidget(m_nav);
    content->addWidget(m_pages, 1);
    rootLayout->addLayout(content, 1);

    // ---- 底部按钮 ----
    auto *buttonRow = new QWidget(this);
    buttonRow->setObjectName("settingsButtonRow");
    auto *buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(16, 10, 16, 12);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    // OK 主按钮（主色实底）与取消按钮样式统一走 applyTheme 的 ID 规则
    if (auto *okBtn = buttons->button(QDialogButtonBox::Ok))
        okBtn->setObjectName("okBtn");
    if (auto *cancelBtn = buttons->button(QDialogButtonBox::Cancel))
        cancelBtn->setObjectName("cancelBtn");
    buttonLayout->addStretch();
    buttonLayout->addWidget(buttons);
    rootLayout->addWidget(buttonRow);

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
        s.setMonitorEnabled(m_monEnabledCheck->isChecked());
        s.setMonitorMaskPasswords(m_monMaskCheck->isChecked());
        s.setMonitorOcrSnapshot(m_monOcrCheck->isChecked());
        s.setMonitorIdleThresholdSec(m_monIdleSpin->value());
        s.setMonitorRetentionDays(m_monRetentionSpin->value());
        s.setAssistantApiKey(m_gdyKeyEdit->text().trimmed());
        s.setAssistantApiBaseUrl(m_gdyUrlEdit->text().trimmed());
        s.setAssistantApiModel(m_gdyModelEdit->text().trimmed());
        s.setAssistantPermMode(m_gdyPermCombo->currentData().toString());
        s.setAssistantSummaryTime(m_gdyTimeEdit->text().trimmed());
        s.setAssistantAutoSummarize(m_gdyAutoCheck->isChecked());
        s.setAssistantBallEnabled(m_gdyBallCheck->isChecked()); // T-050
        s.setAssistantPersonaAppendix(m_gdyAppendix->toPlainText());
    });

    applyTheme();
}

// ---- 各页构建 ----

namespace {
QWidget *newPage()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(20, 16, 20, 16);
    lay->setSpacing(6);
    return page;
}
} // namespace

QWidget *SettingsDialog::buildGeneralPage()
{
    auto *page = newPage();
    auto *lay = qobject_cast<QVBoxLayout *>(page->layout());
    Q_ASSERT(lay);

    auto addSection = [lay](const QString &title) {
        auto *label = new QLabel(title);
        // 颜色继承父级 QLabel 规则（纯黑/纯白），此处只管字重与字号
        label->setStyleSheet("font-weight: bold; font-size: 13px; padding: 8px 0 4px 0;");
        lay->addWidget(label);
    };

    addSection(QStringLiteral("应用"));
    auto *infoLabel = new QLabel(
        QStringLiteral("PopTools —— Windows 全局实用工具箱\n截图（F1）· 贴图（F3）· 划词翻译（Ctrl+Alt+Shift+X）"),
        page);
    lay->addWidget(infoLabel);

    auto *autoStartRow = new QHBoxLayout;
    m_autoStart = new QPushButton(page);
    updateAutoStartButton();
    connect(m_autoStart, &QPushButton::clicked, page, [this] {
        Settings &s = Settings::instance();
        s.setAutoStartEnabled(!s.autoStartEnabled());
        updateAutoStartButton();
    });
    autoStartRow->addWidget(m_autoStart);
    autoStartRow->addStretch();
    lay->addLayout(autoStartRow);

    lay->addStretch();
    return page;
}

QWidget *SettingsDialog::buildCapturePage()
{
    auto *page = newPage();
    auto *lay = qobject_cast<QVBoxLayout *>(page->layout());
    Q_ASSERT(lay);

    auto addSection = [lay](const QString &title) {
        auto *label = new QLabel(title);
        label->setStyleSheet("font-weight: bold; font-size: 13px; padding: 8px 0 4px 0;");
        lay->addWidget(label);
    };
    auto addHint = [lay](const QString &text) {
        auto *label = new QLabel(text);
        lay->addWidget(label);
        return label;
    };

    addSection(QStringLiteral("截图与贴图"));
    addHint(QStringLiteral("区域截图：F1　·　贴图：F3（热键可在 PopTools.ini 中修改）"));
    auto *captureForm = new QFormLayout;
    captureForm->setContentsMargins(0, 6, 0, 0);
    captureForm->setSpacing(6);
    captureForm->setLabelAlignment(Qt::AlignRight);

    auto *dirRow = new QHBoxLayout;
    m_dirEdit = new QLineEdit(Settings::instance().pinSaveDir(), page);
    m_dirEdit->setPlaceholderText("留空不自动保存");
    dirRow->addWidget(m_dirEdit, 1);
    auto *browseBtn = new QPushButton("浏览...", page);
    dirRow->addWidget(browseBtn);
    connect(browseBtn, &QPushButton::clicked, page, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, "选择贴图默认保存文件夹", m_dirEdit->text().trimmed());
        if (!dir.isEmpty())
            m_dirEdit->setText(dir);
    });
    captureForm->addRow("贴图保存:", dirRow);
    lay->addLayout(captureForm);

    lay->addStretch();
    return page;
}

QWidget *SettingsDialog::buildTranslatePage()
{
    auto *page = newPage();
    auto *lay = qobject_cast<QVBoxLayout *>(page->layout());
    Q_ASSERT(lay);

    auto addSection = [lay](const QString &title) {
        auto *label = new QLabel(title);
        label->setStyleSheet("font-weight: bold; font-size: 13px; padding: 8px 0 4px 0;");
        lay->addWidget(label);
    };
    auto addSeparator = [lay, page]() {
        auto *sep = new QFrame(page);
        sep->setObjectName("dialogSeparator"); // 颜色走 applyTheme 的 ID 规则
        sep->setFrameShape(QFrame::HLine);
        lay->addWidget(sep);
    };

    // ---- 翻译 API ----
    addSection("翻译 API");
    auto *apiForm = new QFormLayout;
    apiForm->setContentsMargins(0, 0, 0, 0);
    apiForm->setSpacing(6);
    apiForm->setLabelAlignment(Qt::AlignRight);

    m_formatCombo = new QComboBox(page);
    m_formatCombo->addItem(QStringLiteral("OpenAI 兼容（DeepSeek / Ollama 等）"),
                           QStringLiteral("openai"));
    m_formatCombo->addItem(QStringLiteral("Anthropic（Claude / DeepSeek 兼容端点）"),
                           QStringLiteral("anthropic"));
    // 切换格式时同步 Base URL 占位提示（Anthropic 端点挂在 …/anthropic 前缀下）
    connect(m_formatCombo, &QComboBox::currentIndexChanged, page, [this](int index) {
        m_urlEdit->setPlaceholderText(index == 1
            ? QStringLiteral("https://api.deepseek.com/anthropic")
            : QStringLiteral("https://api.deepseek.com/v1"));
    });
    m_formatCombo->setCurrentIndex(qMax(0, m_formatCombo->findData(Settings::instance().apiFormat())));
    apiForm->addRow("接口格式:", m_formatCombo);

    m_urlEdit = new QLineEdit(Settings::instance().apiBaseUrl(), page);
    m_urlEdit->setPlaceholderText("https://api.deepseek.com/v1");
    apiForm->addRow("Base URL:", m_urlEdit);
    m_keyEdit = new QLineEdit(Settings::instance().apiKey(), page);
    m_keyEdit->setEchoMode(QLineEdit::Password);
    m_keyEdit->setPlaceholderText("sk-xxxxxxxxxxxxxxxx");
    // 密码框看不见内容、粘贴错 Key 无法察觉（401 报错难排查），提供显示切换
    m_keyToggle = new QToolButton(page);
    m_keyToggle->setText(QStringLiteral("显示"));
    m_keyToggle->setToolTip(QStringLiteral("显示/隐藏 API Key，便于核对是否粘贴正确"));
    connect(m_keyToggle, &QToolButton::clicked, page, [this] {
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
    m_modelEdit = new QLineEdit(Settings::instance().apiModel(), page);
    m_modelEdit->setPlaceholderText("deepseek-chat");
    apiForm->addRow("Model:", m_modelEdit);
    lay->addLayout(apiForm);

    // 测试连接：用表单当前值（未保存前也能测）按所选接口格式发一次真实请求，结果就地显示
    m_testBtn = new QPushButton(QStringLiteral("测试连接"), page);
    m_testBtn->setToolTip(QStringLiteral("用上方填写的接口格式 / Base URL / API Key / Model 发起一次真实翻译请求"));
    m_testStatus = new QLabel(page);
    m_tester = new ApiTester(page);
    connect(m_tester, &ApiTester::finished, page, [this](bool ok, const QString &message) {
        m_testBtn->setEnabled(true);
        m_testBtn->setText(QStringLiteral("测试连接"));
        m_testStatus->setText(ok ? QStringLiteral("✓ ") + message
                                 : QStringLiteral("✗ ") + message);
        // 成功/失败色取主题表（此前硬编码浅色值，深色主题不可读）
        m_testStatus->setStyleSheet(QStringLiteral("color: %1;")
                                        .arg(ok ? uiThemeColors(systemSchemeDark()).successText
                                                : uiThemeColors(systemSchemeDark()).errText));
    });
    connect(m_testBtn, &QPushButton::clicked, page, [this] {
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
    lay->addLayout(testRow);

    addSeparator();

    // ---- 翻译领域与术语 ----
    addSection("翻译领域与术语");
    auto *domainForm = new QFormLayout;
    domainForm->setContentsMargins(0, 0, 0, 0);
    domainForm->setSpacing(6);
    domainForm->setLabelAlignment(Qt::AlignRight);

    // 领域：逗号/顿号分隔，注入提示词让模型按领域术语翻译
    m_domainEdit = new QLineEdit(Settings::instance().translateDomains().join(", "), page);
    m_domainEdit->setPlaceholderText("如：医学, 法律, 计算机（逗号分隔，可留空）");
    domainForm->addRow("领域:", m_domainEdit);

    // 专有名词翻译对：每行一条 "原文=译文"，整体注入提示词强制按术语表翻译
    m_termsEdit = new QPlainTextEdit(page);
    QString termLines;
    for (const auto &term : Settings::instance().translateTerms())
        termLines += term.first + "=" + term.second + "\n";
    m_termsEdit->setPlainText(termLines);
    m_termsEdit->setPlaceholderText(
        "每行一条：原文=译文\n如：\nworkflow=工作流\nmyocardial infarction=心肌梗死");
    m_termsEdit->setFixedHeight(88); // 边框/底色/圆角走 applyTheme 的类型规则
    domainForm->addRow("术语表:", m_termsEdit);
    lay->addLayout(domainForm);

    lay->addStretch();
    return page;
}

QWidget *SettingsDialog::buildMonitorPage()
{
    auto *page = newPage();
    auto *lay = qobject_cast<QVBoxLayout *>(page->layout());
    Q_ASSERT(lay);

    auto *label = new QLabel(QStringLiteral("记录"));
    label->setStyleSheet("font-weight: bold; font-size: 13px; padding: 8px 0 4px 0;");
    lay->addWidget(label);

    m_monEnabledCheck = new QCheckBox(
        QStringLiteral("开启活动监控（键入记录 + 窗口时间线 + 中文上屏快照）"), page);
    m_monEnabledCheck->setChecked(Settings::instance().monitorEnabled());
    m_monEnabledCheck->setToolTip(QStringLiteral(
        "开启后 PopTools 将在本地记录你输入的文字与使用的窗口，"
        "供 Goody 管家生成「本日工作记录」。数据只存本机，"
        "生成总结时当日摘要会发送给所配的大模型 API。"));
    lay->addWidget(m_monEnabledCheck);

    m_monMaskCheck = new QCheckBox(QStringLiteral("密码框自动打码（推荐）"), page);
    m_monMaskCheck->setChecked(Settings::instance().monitorMaskPasswords());
    m_monMaskCheck->setToolTip(QStringLiteral(
        "焦点在密码输入框时，键入内容记为 ●●● 占位符（保留长度、不记内容）。"
        "部分浏览器/UWP 自绘密码框可能无法识别，重要密码建议仍避免在监控开启时输入。"));
    lay->addWidget(m_monMaskCheck);

    m_monOcrCheck = new QCheckBox(QStringLiteral("中文上屏 OCR 快照（输入法上屏时截屏识别）"), page);
    m_monOcrCheck->setChecked(Settings::instance().monitorOcrSnapshot());
    m_monOcrCheck->setToolTip(QStringLiteral(
        "全局键盘钩子拿不到输入法的上屏中文，只记得到拼音。开启本项后，"
        "检测到中文上屏（按空格/回车）会截取前台窗口做本地 OCR，"
        "识别出新增文字后立即删除截图。"));
    lay->addWidget(m_monOcrCheck);

    auto addSeparator = [lay, page]() {
        auto *sep = new QFrame(page);
        sep->setObjectName("dialogSeparator"); // 颜色走 applyTheme 的 ID 规则
        sep->setFrameShape(QFrame::HLine);
        lay->addWidget(sep);
    };
    addSeparator();

    auto *section2 = new QLabel(QStringLiteral("参数"));
    section2->setStyleSheet("font-weight: bold; font-size: 13px; padding: 8px 0 4px 0;");
    lay->addWidget(section2);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    m_monIdleSpin = new QSpinBox(page);
    m_monIdleSpin->setRange(10, 3600);
    m_monIdleSpin->setSuffix(QStringLiteral(" 秒"));
    m_monIdleSpin->setValue(Settings::instance().monitorIdleThresholdSec());
    m_monIdleSpin->setToolTip(QStringLiteral("键鼠无输入超过该时长即判定离开，工作时长不再累计"));
    form->addRow(QStringLiteral("判定离开的空闲时长:"), m_monIdleSpin);

    m_monRetentionSpin = new QSpinBox(page);
    m_monRetentionSpin->setRange(1, 3650);
    m_monRetentionSpin->setSuffix(QStringLiteral(" 天"));
    m_monRetentionSpin->setValue(Settings::instance().monitorRetentionDays());
    m_monRetentionSpin->setToolTip(QStringLiteral("活动日志按天存放，超期目录在启动时自动删除"));
    form->addRow(QStringLiteral("日志保留天数:"), m_monRetentionSpin);
    lay->addLayout(form);

    auto *hint = new QLabel(QStringLiteral(
        "日志位置：%LOCALAPPDATA%\\PopTools\\monitor\\<日期>\\（JSONL 文本，可用记事本打开）。\n"
        "托盘菜单可一键启停；Goody 管家（内测中）将基于这些日志生成每日工作记录。"));
    hint->setWordWrap(true);
    // 提示文字颜色交给 applyTheme 的 QLabel 规则（纯黑/纯白随主题，
    // T-030 约定：文字不允许灰色）；此处只约束字号
    hint->setStyleSheet("font-size: 12px;");
    lay->addWidget(hint);

    lay->addStretch();
    return page;
}

QWidget *SettingsDialog::buildGoodyPage()
{
    auto *page = newPage();
    auto *lay = qobject_cast<QVBoxLayout *>(page->layout());
    Q_ASSERT(lay);

    auto addSection = [lay](const QString &title) {
        auto *label = new QLabel(title);
        label->setStyleSheet("font-weight: bold; font-size: 13px; padding: 8px 0 4px 0;");
        lay->addWidget(label);
    };
    auto addSeparator = [lay, page]() {
        auto *sep = new QFrame(page);
        sep->setObjectName("dialogSeparator");
        sep->setFrameShape(QFrame::HLine);
        lay->addWidget(sep);
    };

    // ---- 大模型 API ----
    addSection(QStringLiteral("大模型 API（OpenAI 兼容）"));
    auto *apiForm = new QFormLayout;
    apiForm->setLabelAlignment(Qt::AlignRight);
    m_gdyKeyEdit = new QLineEdit(page);
    m_gdyKeyEdit->setEchoMode(QLineEdit::Password);
    m_gdyKeyEdit->setPlaceholderText(QStringLiteral("sk-…"));
    m_gdyKeyEdit->setText(Settings::instance().assistantApiKey());
    apiForm->addRow(QStringLiteral("API Key:"), m_gdyKeyEdit);
    m_gdyUrlEdit = new QLineEdit(page);
    m_gdyUrlEdit->setText(Settings::instance().assistantApiBaseUrl());
    apiForm->addRow(QStringLiteral("Base URL:"), m_gdyUrlEdit);
    m_gdyModelEdit = new QLineEdit(page);
    m_gdyModelEdit->setText(Settings::instance().assistantApiModel());
    m_gdyModelEdit->setToolTip(QStringLiteral("调试推荐 deepseek-v4-flash（便宜快）；正式总结可换 deepseek-chat"));
    apiForm->addRow(QStringLiteral("模型:"), m_gdyModelEdit);
    lay->addLayout(apiForm);

    addSeparator();

    // ---- 权限与每日记录 ----
    addSection(QStringLiteral("权限与每日记录"));
    auto *permForm = new QFormLayout;
    permForm->setLabelAlignment(Qt::AlignRight);
    m_gdyPermCombo = new QComboBox(page);
    m_gdyPermCombo->addItem(QStringLiteral("逐条确认（写文件/执行命令前都问我）"),
                            QStringLiteral("ask"));
    m_gdyPermCombo->addItem(QStringLiteral("智能自动（安全命令白名单内自动执行）"),
                            QStringLiteral("smart"));
    m_gdyPermCombo->addItem(QStringLiteral("全自动（不再询问，风险自负）"), QStringLiteral("auto"));
    const QString permMode = Settings::instance().assistantPermMode();
    for (int i = 0; i < m_gdyPermCombo->count(); ++i)
        if (m_gdyPermCombo->itemData(i).toString() == permMode)
            m_gdyPermCombo->setCurrentIndex(i);
    permForm->addRow(QStringLiteral("权限模式:"), m_gdyPermCombo);
    m_gdyTimeEdit = new QLineEdit(page);
    m_gdyTimeEdit->setText(Settings::instance().assistantSummaryTime());
    m_gdyTimeEdit->setPlaceholderText(QStringLiteral("22:00"));
    m_gdyTimeEdit->setToolTip(QStringLiteral("每天到点后检查活动日志，为有记录但未总结的日子生成「本日工作记录」"));
    // 输入即校验 HH:mm（R3 #15）：坏值此前会直写 ini，读端静默回退 22:00 无提示
    m_gdyTimeEdit->setValidator(
        new QRegularExpressionValidator(QRegularExpression(QStringLiteral(
            "^([01]?\\d|2[0-3]):[0-5]\\d$")), m_gdyTimeEdit));
    permForm->addRow(QStringLiteral("每日总结时刻:"), m_gdyTimeEdit);
    m_gdyAutoCheck = new QCheckBox(QStringLiteral("自动生成每日工作记录（启动时补账 + 定时）"), page);
    m_gdyAutoCheck->setChecked(Settings::instance().assistantAutoSummarize());
    permForm->addRow(QString(), m_gdyAutoCheck);
    lay->addLayout(permForm);

    addSeparator();

    // ---- 桌面悬浮球（T-050） ----
    addSection(QStringLiteral("桌面悬浮球"));
    m_gdyBallCheck = new QCheckBox(
        QStringLiteral("在桌面显示 Goody 悬浮球（单击打开对话，可拖动并记忆位置；"
                       "Goody 工作时呼吸光效提示）"), page);
    m_gdyBallCheck->setChecked(Settings::instance().assistantBallEnabled());
    lay->addWidget(m_gdyBallCheck);

    addSeparator();

    // ---- 人格附言 ----
    addSection(QStringLiteral("人格附言（可选，追加到 Goody 系统提示词末尾）"));
    m_gdyAppendix = new QPlainTextEdit(page);
    m_gdyAppendix->setPlaceholderText(QStringLiteral("例如：称呼我为\"老板\"；记录风格口语化一点…"));
    m_gdyAppendix->setMaximumHeight(80);
    m_gdyAppendix->setPlainText(Settings::instance().assistantPersonaAppendix());
    lay->addWidget(m_gdyAppendix);

    auto *hint = new QLabel(QStringLiteral(
        "呼出方式：Ctrl+Alt+G 或托盘菜单「Goody 管家」。\n"
        "Key 只保存在本机 PopTools.ini，不会随仓库提交。"));
    hint->setWordWrap(true);
    // 提示文字颜色交给 applyTheme 的 QLabel 规则（纯黑/纯白随主题，
    // T-030 约定：文字不允许灰色）；此处只约束字号
    hint->setStyleSheet("font-size: 12px;");
    lay->addWidget(hint);

    lay->addStretch();
    return page;
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

// 主题应用（构造末尾与 colorSchemeChanged 时调用）：整套样式从 ThemeColors
// 取值。左侧导航 + 分页 + 控件 6px 圆角 + OK 主按钮主色实底
void SettingsDialog::applyTheme()
{
    const ThemeColors c = uiThemeColors(systemSchemeDark());
    // 导航底色/选中底色为本对话框专属，不入全局 ThemeColors 表：
    // 选中底 = 强调色低透明（QSS #AARRGGBB），保证两套主题都可读
    const QString navBg = c.dark ? "#242424" : "#F5F6F8";
    const QString navSel = QStringLiteral("#2E%1").arg(QString(c.accent).mid(1));

    setStyleSheet(QStringLiteral(
        "#settingsDialog { background: %1; }"
        "#settingsButtonRow { background: %1; border-top: 1px solid %10; }"
        "QListWidget#settingsNav { background: %11; border: none;"
        "  border-right: 1px solid %10; outline: 0; padding-top: 8px; }"
        "QListWidget#settingsNav::item { color: %4; padding: 9px 16px;"
        "  border-left: 3px solid transparent; }"
        "QListWidget#settingsNav::item:selected { background: %12;"
        "  border-left: 3px solid %5; }"
        "QStackedWidget#settingsPages { background: %1; }"
        "QLineEdit { background: %2; border: 1px solid %3; border-radius: 6px;"
        "  padding: 4px 8px; color: %4; }"
        "QLineEdit:focus { border-color: %5; }"
        "QPlainTextEdit { background: %2; border: 1px solid %3; border-radius: 6px;"
        "  padding: 4px 8px; color: %4; }"
        "QPlainTextEdit:focus { border-color: %5; }"
        "QComboBox { background: %2; border: 1px solid %3; border-radius: 6px;"
        "  padding: 3px 8px; color: %4; }"
        "QPushButton { background: %6; border: 1px solid %7; border-radius: 6px;"
        "  padding: 4px 12px; color: %4; }"
        "QPushButton:hover { background: %8; border-color: %5; color: %5; }"
        "QPushButton:disabled { color: %4; }"
        "QCheckBox { color: %4; }"
        "QLabel { color: %4; }"
        "QToolButton { background: %6; border: 1px solid %7; border-radius: 6px;"
        "  padding: 3px 8px; color: %4; }"
        "QToolButton:hover { background: %8; border-color: %5; color: %5; }"
        "QSlider::groove:horizontal { height: 4px; background: %10; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { height: 4px; background: %5; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 14px; margin: -6px 0;"
        "  border-radius: 7px; background: %1; border: 2px solid %5; }"
        "#dialogSeparator { color: %10; margin: 8px 0; }"
        "#okBtn { background: %5; color: #FFFFFF; border: 1px solid %5;"
        "  padding: 5px 16px; min-width: 70px; font-weight: 600; }"
        "#okBtn:hover { background: %9; }"
        "#cancelBtn { padding: 5px 16px; min-width: 70px; }")
        .arg(c.cardBg, c.editBg, c.editBorder, c.body, c.accent,
             c.ctrlBg, c.ctrlBorder, c.ctrlHover, c.accentHover)
        .arg(c.line, navBg, navSel));
}

} // namespace poptools
