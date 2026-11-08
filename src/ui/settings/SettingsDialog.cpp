#include "SettingsDialog.h"
#include "CategoryManagerDialog.h"
#include "core/calendar/CalendarStore.h"
#include "core/settings/Settings.h"
#include "core/translation/ApiTester.h"
#include "ui/ThemeColors.h"

#include <QCheckBox>
#include <QColorDialog>
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
#include <QSlider>
#include <QStackedWidget>
#include <QStyleHints>
#include <QToolButton>
#include <QVBoxLayout>

namespace poptools {

SettingsDialog::SettingsDialog(CalendarStore *calendarStore, QWidget *parent)
    : QDialog(parent)
    , m_calendarStore(calendarStore)
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
    m_nav->addItem(QStringLiteral("桌面日历"));

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName("settingsPages");
    m_pages->addWidget(buildGeneralPage());
    m_pages->addWidget(buildCapturePage());
    m_pages->addWidget(buildTranslatePage());
    m_pages->addWidget(buildCalendarPage());

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
        s.setCalendarEnabled(m_calShowCheck->isChecked());
        s.setCalendarOpacity(m_calOpacity->value());
        s.setCalendarBgColor(m_calBg);
        s.setCalendarAccentColor(m_calAccent);
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
        QStringLiteral("PopTools —— Windows 全局实用工具箱\n截图（F1）· 贴图（F3）· 划词翻译（Ctrl+Alt+Shift+X）· 桌面日历"),
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

QWidget *SettingsDialog::buildCalendarPage()
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

    // ---- 显示 ----
    addSection(QStringLiteral("显示"));
    m_calShowCheck = new QCheckBox(QStringLiteral("显示桌面日历（贴在桌面上，被普通窗口遮挡）"), page);
    m_calShowCheck->setChecked(Settings::instance().calendarEnabled());
    m_calShowCheck->setToolTip(QStringLiteral("勾选后立即显示并随应用启动自动出现；日历窗口内 ✏️ 可进入编辑模式调整位置大小"));
    lay->addWidget(m_calShowCheck);

    addSeparator();

    // ---- 外观 ----
    addSection(QStringLiteral("外观"));
    auto *opacityRow = new QHBoxLayout;
    opacityRow->setSpacing(8);
    opacityRow->addWidget(new QLabel(QStringLiteral("整体透明度:"), page));
    m_calOpacity = new QSlider(Qt::Horizontal, page);
    m_calOpacity->setRange(20, 100);
    m_calOpacity->setValue(Settings::instance().calendarOpacity());
    m_calOpacity->setFixedWidth(220);
    m_calOpacityValue = new QLabel(page);
    auto updateOpacityLabel = [this] {
        m_calOpacityValue->setText(QStringLiteral("%1%").arg(m_calOpacity->value()));
    };
    connect(m_calOpacity, &QSlider::valueChanged, page, updateOpacityLabel);
    updateOpacityLabel();
    opacityRow->addWidget(m_calOpacity);
    opacityRow->addWidget(m_calOpacityValue);
    opacityRow->addStretch();
    lay->addLayout(opacityRow);

    auto *colorForm = new QFormLayout;
    colorForm->setContentsMargins(0, 4, 0, 0);
    colorForm->setSpacing(6);
    colorForm->setLabelAlignment(Qt::AlignRight);

    m_calBg = Settings::instance().calendarBgColor();
    m_calAccent = Settings::instance().calendarAccentColor();

    m_calBgSwatch = new QLabel(page);
    m_calBgSwatch->setFixedSize(48, 20);
    auto *bgPickBtn = new QPushButton(QStringLiteral("修改…"), page);
    connect(bgPickBtn, &QPushButton::clicked, page, [this] {
        const QColor c = QColorDialog::getColor(m_calBg, this, QStringLiteral("背景色"));
        if (c.isValid()) {
            m_calBg = c;
            updateCalendarColorButtons();
        }
    });
    auto *bgRow = new QHBoxLayout;
    bgRow->setSpacing(8);
    bgRow->addWidget(m_calBgSwatch);
    bgRow->addWidget(bgPickBtn);
    bgRow->addStretch();
    colorForm->addRow(QStringLiteral("背景色:"), bgRow);

    m_calAccentSwatch = new QLabel(page);
    m_calAccentSwatch->setFixedSize(48, 20);
    auto *accentPickBtn = new QPushButton(QStringLiteral("修改…"), page);
    connect(accentPickBtn, &QPushButton::clicked, page, [this] {
        const QColor c = QColorDialog::getColor(m_calAccent, this, QStringLiteral("强调色"));
        if (c.isValid()) {
            m_calAccent = c;
            updateCalendarColorButtons();
        }
    });
    auto *accentRow = new QHBoxLayout;
    accentRow->setSpacing(8);
    accentRow->addWidget(m_calAccentSwatch);
    accentRow->addWidget(accentPickBtn);
    accentRow->addStretch();
    colorForm->addRow(QStringLiteral("强调色:"), accentRow);
    lay->addLayout(colorForm);

    addSeparator();

    // ---- 分类 ----
    addSection(QStringLiteral("分类"));
    auto *categoryRow = new QHBoxLayout;
    auto *categoryBtn = new QPushButton(QStringLiteral("管理分类…"), page);
    categoryBtn->setToolTip(QStringLiteral("新增/重命名分类与颜色；删除分类后其条目归入「其他」"));
    categoryBtn->setEnabled(m_calendarStore != nullptr);
    connect(categoryBtn, &QPushButton::clicked, page, [this] {
        CategoryManagerDialog dlg(m_calendarStore, this);
        dlg.exec();
    });
    categoryRow->addWidget(categoryBtn);
    categoryRow->addStretch();
    lay->addLayout(categoryRow);

    lay->addStretch();
    return page;
}

void SettingsDialog::updateCalendarColorButtons()
{
    if (m_calBgSwatch) {
        m_calBgSwatch->setStyleSheet(
            QStringLiteral("background: %1; border: 1px solid %2; border-radius: 4px;")
                .arg(m_calBg.name(QColor::HexRgb),
                     uiThemeColors(systemSchemeDark()).editBorder));
        m_calBgSwatch->setText(QString());
    }
    if (m_calAccentSwatch) {
        m_calAccentSwatch->setStyleSheet(
            QStringLiteral("background: %1; border: 1px solid %2; border-radius: 4px;")
                .arg(m_calAccent.name(QColor::HexRgb),
                     uiThemeColors(systemSchemeDark()).editBorder));
        m_calAccentSwatch->setText(QString());
    }
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

    updateCalendarColorButtons();
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
