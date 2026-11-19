#include "SettingsDialog.h"
#include "SettingCard.h"
#include "ToggleSwitch.h"
#include "app/AppServices.h"
#include "core/calendar/CalendarSettings.h"
#include "core/settings/Settings.h"
#include "core/translation/ApiTester.h"
#include "ui/ThemeColors.h"
#include "ui/theming/FluentTheme.h"

#include <algorithm>
#include <functional>

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
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
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyleHints>
#include <QTime>
#include <QToolButton>
#include <QVBoxLayout>

namespace poptools {

// services 可为空（单测场景，日历功能按钮全部静默；即时广播跳过）；主题
// 跟随系统明暗，QSS 全量重设（applyTheme）

SettingsDialog::SettingsDialog(AppServices *services, QWidget *parent)
    : QDialog(parent)
    , m_services(services)
{
    setWindowTitle("PopTools 设置");
    setMinimumSize(880, 600);
    resize(920, 660);
    setObjectName("settingsDialog");

    // 主题化（跟随系统明暗）：P-022——自定义类一律 objectName ID 选择器；
    // Qt 内置类用类型选择器
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, &SettingsDialog::applyTheme);

    // 文本编辑提交防抖（QPlainTextEdit 每键触发 textChanged，400ms 合帧落盘）
    m_textCommitTimer.setSingleShot(true);
    m_textCommitTimer.setInterval(400);
    connect(&m_textCommitTimer, &QTimer::timeout, this, &SettingsDialog::commitSettings);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ---- 内容区：左导航 + 右滚动页 ----
    auto *content = new QHBoxLayout;
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(0);

    auto *navPanel = new QWidget(this);
    navPanel->setObjectName("settingsNav");
    navPanel->setFixedWidth(210);
    auto *navLayout = new QVBoxLayout(navPanel);
    navLayout->setContentsMargins(12, 20, 12, 16);
    navLayout->setSpacing(10);

    auto *navTitle = new QLabel(QStringLiteral("设置"), navPanel);
    navTitle->setObjectName("navTitle");
    navLayout->addWidget(navTitle);

    m_nav = new QListWidget(navPanel);
    m_nav->setObjectName("settingsNavList");
    m_nav->setFrameShape(QFrame::NoFrame);
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->addItem(QStringLiteral("通用"));
    m_nav->addItem(QStringLiteral("截图与贴图"));
    m_nav->addItem(QStringLiteral("翻译"));
    m_nav->addItem(QStringLiteral("活动监控"));
    m_nav->addItem(QStringLiteral("Goody 管家"));
    m_nav->addItem(QStringLiteral("桌面日历"));

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName("settingsPages");
    const auto wrapInScroll = [](QWidget *page) {
        auto *scroll = new QScrollArea;
        scroll->setObjectName("settingsPageScroll");
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(page);
        return scroll;
    };
    m_pages->addWidget(wrapInScroll(buildGeneralPage()));
    m_pages->addWidget(wrapInScroll(buildCapturePage()));
    m_pages->addWidget(wrapInScroll(buildTranslatePage()));
    m_pages->addWidget(wrapInScroll(buildMonitorPage()));
    m_pages->addWidget(wrapInScroll(buildGoodyPage()));
    m_pages->addWidget(wrapInScroll(buildCalendarPage()));

    connect(m_nav, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_nav->setCurrentRow(0);

    navLayout->addWidget(m_nav);
    navLayout->addStretch();
    content->addWidget(navPanel);
    content->addWidget(m_pages, 1);
    rootLayout->addLayout(content, 1);

    // ---- 即时生效：控件变更 → commitSettings（连接置后，避开构建期
    // setCurrentIndex 等触发的多余提交；commitSettings 内另有控件齐套防御） ----
    const std::initializer_list<QWidget *> instantControls = {
        m_formatCombo, m_urlEdit, m_keyEdit, m_modelEdit, m_domainEdit, m_termsEdit,
        m_dirEdit, m_monEnabledSwitch, m_monOcrSwitch, m_monIdleSpin, m_monRetentionSpin,
        m_gdyKeyEdit, m_gdyUrlEdit, m_gdyModelEdit, m_gdyPermCombo, m_gdyTimeEdit,
        m_gdyAutoSwitch, m_gdyBallSwitch, m_gdyAppendix,
        m_calLunarSwitch, m_calWeekNumSwitch, m_calEventsSwitch, m_calGlassSwitch,
        m_calGuideSwitch, m_calListSwitch, m_calFontSpin,
    };
    for (QWidget *w : instantControls)
        connectInstant(w);

    applyTheme();
}

void SettingsDialog::openCalendarPage()
{
    m_nav->setCurrentRow(m_nav->count() - 1); // 桌面日历页固定在导航末位
}

// 关闭前冲刷：文本防抖窗口期内关窗（Esc/标题栏 ✕）不丢最后一次编辑
void SettingsDialog::done(int r)
{
    flushPendingCommit();
    QDialog::done(r);
}

void SettingsDialog::scheduleTextCommit()
{
    m_textCommitTimer.start();
}

void SettingsDialog::flushPendingCommit()
{
    if (m_textCommitTimer.isActive()) {
        m_textCommitTimer.stop();
        commitSettings();
    }
}

// 按控件类型连接变更信号 → commitSettings（ToggleSwitch 是 QAbstractButton
// 派生，命中 toggled 分支）
void SettingsDialog::connectInstant(QWidget *w)
{
    if (auto *b = qobject_cast<QAbstractButton *>(w))
        connect(b, &QAbstractButton::toggled, this, &SettingsDialog::commitSettings);
    else if (auto *c = qobject_cast<QComboBox *>(w))
        connect(c, &QComboBox::currentIndexChanged, this, &SettingsDialog::commitSettings);
    else if (auto *sp = qobject_cast<QSpinBox *>(w))
        connect(sp, &QSpinBox::valueChanged, this, &SettingsDialog::commitSettings);
    else if (auto *le = qobject_cast<QLineEdit *>(w))
        connect(le, &QLineEdit::editingFinished, this, &SettingsDialog::commitSettings);
    else if (auto *pe = qobject_cast<QPlainTextEdit *>(w))
        connect(pe, &QPlainTextEdit::textChanged, this, &SettingsDialog::scheduleTextCommit);
}

// 即时生效提交：表单当前值 → Settings / CalendarSettings → 广播。开窗期间
// 每次变更都会走到这里（文本编辑经 400ms 防抖合并）
void SettingsDialog::commitSettings()
{
    // 控件齐套防御：任何分页控件为空说明构建未完成，不落盘
    if (!m_formatCombo || !m_calFontSpin)
        return;

    Settings &s = Settings::instance();
    s.beginBatch(); // T-071：~20 个 setter 合并为一次整 INI 落盘（endBatch 收口）
    s.setApiFormat(m_formatCombo->currentData().toString());
    s.setApiBaseUrl(m_urlEdit->text().trimmed());
    s.setApiKey(m_keyEdit->text().trimmed());
    s.setApiModel(m_modelEdit->text().trimmed());
    s.setTranslateDomains(parseDomains());
    s.setTranslateTerms(parseTerms());
    s.setPinSaveDir(m_dirEdit->text().trimmed());
    s.setMonitorEnabled(m_monEnabledSwitch->isChecked());
    s.setMonitorOcrSnapshot(m_monOcrSwitch->isChecked());
    s.setMonitorIdleThresholdSec(m_monIdleSpin->value());
    s.setMonitorRetentionDays(m_monRetentionSpin->value());
    s.setAssistantApiKey(m_gdyKeyEdit->text().trimmed());
    s.setAssistantApiBaseUrl(m_gdyUrlEdit->text().trimmed());
    s.setAssistantApiModel(m_gdyModelEdit->text().trimmed());
    s.setAssistantPermMode(m_gdyPermCombo->currentData().toString());
    // T-062 修复: HH:mm 校验器允许 Intermediate 残留（如“2:”），无效时刻
    // 不落盘，保留磁盘上的最近有效值（读侧另有 22:00 回退兜底）
    const QString summaryTime = m_gdyTimeEdit->text().trimmed();
    s.setAssistantSummaryTime(QTime::fromString(summaryTime, QStringLiteral("HH:mm")).isValid()
                                  ? summaryTime
                                  : Settings::instance().assistantSummaryTime());
    s.setAssistantAutoSummarize(m_gdyAutoSwitch->isChecked());
    s.setAssistantBallEnabled(m_gdyBallSwitch->isChecked()); // T-050
    s.setAssistantPersonaAppendix(m_gdyAppendix->toPlainText());
    s.endBatch(); // 批量写收口：一次整 INI sync（其后日历 JSON 与广播不变）

    // 桌面日历：load→改→save 只覆写本页暴露的 7 个字段；几何(x/y/width/height)
    // 与 showNextMonthPill 取磁盘最新值。写入 JSON 后经能力槽通知宿主重载重绘
    CalendarSettings cal = CalendarSettings::load();
    cal.showLunar = m_calLunarSwitch->isChecked();
    cal.showWeekNumbers = m_calWeekNumSwitch->isChecked();
    cal.showEvents = m_calEventsSwitch->isChecked();
    cal.glassEffect = m_calGlassSwitch->isChecked();
    cal.showGuide = m_calGuideSwitch->isChecked();
    cal.hidden = !m_calListSwitch->isChecked();
    cal.fontScale = m_calFontSpin->value();
    cal.save();
    if (m_services && m_services->calendarApplySettings)
        m_services->calendarApplySettings();

    // 即时生效广播：有缓存外观的模块重应用（原 Accepted 后统一广播的路径，
    // 改版后由这里逐次触发；m_services 为空=单测场景跳过）
    if (m_services)
        m_services->notifySettingsChanged();
}

// ---- 各页构建 ----

namespace {
// 页面骨架：页标题 + 纵排分组容器（滚动包裹在构造里完成）
QWidget *newPage(const QString &title)
{
    auto *page = new QWidget;
    page->setObjectName("settingsPage");
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(32, 28, 32, 24);
    lay->setSpacing(16);
    auto *titleLabel = new QLabel(title, page);
    titleLabel->setObjectName("settingsPageTitle");
    lay->addWidget(titleLabel);
    lay->addSpacing(6);
    return page;
}

// 提示行：自动换行小字（色取 textTertiary 档）
QLabel *makeHint(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName("settingsHint");
    label->setWordWrap(true);
    return label;
}
} // namespace

// 通用页：应用简介 + 开机启动开关。开关即点即写注册表（HKCU Run），
// 不经 commitSettings（自有持久化路径），文案按钮自翻转
QWidget *SettingsDialog::buildGeneralPage()
{
    auto *page = newPage(QStringLiteral("通用"));
    auto *lay = qobject_cast<QVBoxLayout *>(page->layout());
    Q_ASSERT(lay);

    auto *group = new SettingCardGroup(QStringLiteral("应用"), page);
    lay->addWidget(group);

    group->addCard(new SettingCard(
        QStringLiteral("PopTools"),
        QStringLiteral("Windows 全局实用工具箱——区域截图（F1）· 贴图（F3）· "
                       "划词翻译（Ctrl+Alt+Shift+X）"),
        page));

    auto *autoCard = new SettingCard(
        QStringLiteral("开机启动"),
        QStringLiteral("登录 Windows 时自动在后台运行 PopTools（托盘常驻）"), page);
    m_autoStart = new QPushButton(autoCard);
    updateAutoStartButton();
    connect(m_autoStart, &QPushButton::clicked, page, [this] {
        Settings &s = Settings::instance();
        s.setAutoStartEnabled(!s.autoStartEnabled());
        updateAutoStartButton();
    });
    autoCard->setTrailingWidget(m_autoStart);
    group->addCard(autoCard);

    // ---- 收纳盒（T-069b 菜单精简：托盘「收纳盒管理/隐藏收纳盒」的等价入口） ----
    auto *boxGroup = new SettingCardGroup(QStringLiteral("收纳盒"), page);
    lay->addWidget(boxGroup);

    auto *boxManageCard = new SettingCard(
        QStringLiteral("收纳盒管理"),
        QStringLiteral("新建空盒 / 绑定文件夹 / 改名 / 解除绑定 / 调整位置和列数"), page);
    auto *boxManageBtn = new QPushButton(QStringLiteral("打开"), boxManageCard);
    connect(boxManageBtn, &QPushButton::clicked, this, [this] {
        if (m_services && m_services->boxOpenManager)
            m_services->boxOpenManager();
    });
    boxManageCard->setTrailingWidget(boxManageBtn);
    boxGroup->addCard(boxManageCard);

    auto *boxVisCard = new SettingCard(
        QStringLiteral("显示收纳盒"),
        QStringLiteral("全部显示或全部隐藏（盒头 ✛ 也可单独调整）"), page);
    m_boxVisibleSwitch = new ToggleSwitch(boxVisCard);
    m_boxVisibleSwitch->setChecked(m_services && m_services->boxAnyVisible
                                       ? m_services->boxAnyVisible()
                                       : true);
    connect(m_boxVisibleSwitch, &QAbstractButton::toggled, this, [this](bool visible) {
        if (m_services && m_services->boxSetVisible)
            m_services->boxSetVisible(visible); // 即时槽调用，不经 commitSettings
    });
    boxVisCard->setTrailingWidget(m_boxVisibleSwitch);
    boxGroup->addCard(boxVisCard);

    lay->addStretch();
    return page;
}

// 截图与贴图页：贴图默认保存目录。留空 = F3 不自动落盘；目录不存在由
// PinWindow 落盘侧 mkpath 兜底（PinWindow.cpp:129 saveToDefaultDir），此处不校验
QWidget *SettingsDialog::buildCapturePage()
{
    auto *page = newPage(QStringLiteral("截图与贴图"));
    auto *lay = qobject_cast<QVBoxLayout *>(page->layout());
    Q_ASSERT(lay);

    auto *group = new SettingCardGroup(QStringLiteral("截图与贴图"), page);
    lay->addWidget(group);

    auto *dirCard = new SettingCard(
        QStringLiteral("贴图默认保存文件夹"),
        QStringLiteral("关闭贴图（F3）时自动把原图存到此文件夹；留空则不自动保存"), page);
    auto *dirRow = new QWidget(dirCard);
    auto *dirLay = new QHBoxLayout(dirRow);
    dirLay->setContentsMargins(0, 0, 0, 0);
    dirLay->setSpacing(8);
    m_dirEdit = new QLineEdit(Settings::instance().pinSaveDir(), dirRow);
    m_dirEdit->setPlaceholderText("留空不自动保存");
    m_dirEdit->setMinimumWidth(280);
    dirLay->addWidget(m_dirEdit);
    auto *browseBtn = new QPushButton("浏览...", dirRow);
    dirLay->addWidget(browseBtn);
    connect(browseBtn, &QPushButton::clicked, page, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, "选择贴图默认保存文件夹", m_dirEdit->text().trimmed());
        if (!dir.isEmpty())
            m_dirEdit->setText(dir);
    });
    dirCard->setTrailingWidget(dirRow);
    group->addCard(dirCard);

    lay->addStretch();
    return page;
}

// 翻译页：API 卡片组 + 测试连接（用当前值发真实请求）+ 领域/术语表注入项。
// 切换格式仅改 Base URL 占位提示
QWidget *SettingsDialog::buildTranslatePage()
{
    auto *page = newPage(QStringLiteral("翻译"));
    auto *lay = qobject_cast<QVBoxLayout *>(page->layout());
    Q_ASSERT(lay);

    // ---- 翻译 API ----
    auto *apiGroup = new SettingCardGroup(QStringLiteral("翻译 API"), page);
    lay->addWidget(apiGroup);

    auto *formatCard = new SettingCard(
        QStringLiteral("接口格式"),
        QStringLiteral("OpenAI 兼容（DeepSeek / Ollama 等）或 Anthropic（Claude / "
                       "DeepSeek 兼容端点）"), page);
    m_formatCombo = new QComboBox(formatCard);
    m_formatCombo->addItem(QStringLiteral("OpenAI 兼容"),
                           QStringLiteral("openai"));
    m_formatCombo->addItem(QStringLiteral("Anthropic"),
                           QStringLiteral("anthropic"));
    // T-062 修复: connect 原在 m_urlEdit 构造之前，setCurrentIndex 触发
    // currentIndexChanged 时 m_urlEdit 尚为 nullptr——构造/连接/设初值全部
    // 提前到 setCurrentIndex 前（改版后连接集中到构造末尾，本序约束仍在）
    m_urlEdit = new QLineEdit(Settings::instance().apiBaseUrl(), formatCard);
    m_urlEdit->setPlaceholderText("https://api.deepseek.com/v1");
    m_urlEdit->setMinimumWidth(300);
    // 切换格式时同步 Base URL 占位提示（Anthropic 端点挂在 …/anthropic 前缀下）
    connect(m_formatCombo, &QComboBox::currentIndexChanged, formatCard, [this](int index) {
        m_urlEdit->setPlaceholderText(index == 1
            ? QStringLiteral("https://api.deepseek.com/anthropic")
            : QStringLiteral("https://api.deepseek.com/v1"));
    });
    // apiFormat() 读侧已白名单归一（Settings.cpp），返回值恒在 combo 两项之内；
    // qMax(0,…) 仅为防御未来 findData 失配（如新增枚举忘同步 addItem）
    m_formatCombo->setCurrentIndex(qMax(0, m_formatCombo->findData(Settings::instance().apiFormat())));
    formatCard->setTrailingWidget(m_formatCombo);
    apiGroup->addCard(formatCard);

    auto *urlCard = new SettingCard(QStringLiteral("Base URL"),
                                    QStringLiteral("API 服务地址，含版本路径"), page);
    urlCard->setTrailingWidget(m_urlEdit);
    apiGroup->addCard(urlCard);

    auto *keyCard = new SettingCard(QStringLiteral("API Key"),
                                    QStringLiteral("只保存在本机 PopTools.ini，不会随仓库提交"), page);
    m_keyEdit = new QLineEdit(Settings::instance().apiKey(), keyCard);
    m_keyEdit->setEchoMode(QLineEdit::Password);
    m_keyEdit->setPlaceholderText("sk-xxxxxxxxxxxxxxxx");
    m_keyEdit->setMinimumWidth(300);
    // 密码框看不见内容、粘贴错 Key 无法察觉（401 报错难排查），提供显示切换
    m_keyToggle = new QToolButton(keyCard);
    m_keyToggle->setText(QStringLiteral("显示"));
    m_keyToggle->setToolTip(QStringLiteral("显示/隐藏 API Key，便于核对是否粘贴正确"));
    connect(m_keyToggle, &QToolButton::clicked, keyCard, [this] {
        const bool show = m_keyEdit->echoMode() == QLineEdit::Password;
        m_keyEdit->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
        m_keyToggle->setText(show ? QStringLiteral("隐藏") : QStringLiteral("显示"));
    });
    auto *keyRow = new QWidget(keyCard);
    auto *keyLay = new QHBoxLayout(keyRow);
    keyLay->setContentsMargins(0, 0, 0, 0);
    keyLay->setSpacing(8);
    keyLay->addWidget(m_keyEdit);
    keyLay->addWidget(m_keyToggle);
    keyCard->setTrailingWidget(keyRow);
    apiGroup->addCard(keyCard);

    auto *modelCard = new SettingCard(QStringLiteral("模型"),
                                      QStringLiteral("如 deepseek-chat"), page);
    m_modelEdit = new QLineEdit(Settings::instance().apiModel(), modelCard);
    m_modelEdit->setPlaceholderText("deepseek-chat");
    m_modelEdit->setMinimumWidth(300);
    modelCard->setTrailingWidget(m_modelEdit);
    apiGroup->addCard(modelCard);

    auto *testCard = new SettingCard(
        QStringLiteral("测试连接"),
        QStringLiteral("用上方当前值按所选接口格式发一次真实翻译请求"), page);
    m_testBtn = new QPushButton(QStringLiteral("测试连接"), testCard);
    m_testBtn->setToolTip(QStringLiteral("用上方填写的接口格式 / Base URL / API Key / Model 发起一次真实翻译请求"));
    m_testStatus = new QLabel(testCard);
    m_tester = new ApiTester(testCard);
    connect(m_tester, &ApiTester::finished, testCard, [this](bool ok, const QString &message) {
        m_testBtn->setEnabled(true);
        m_testBtn->setText(QStringLiteral("测试连接"));
        m_testStatus->setText(ok ? QStringLiteral("✓ ") + message
                                 : QStringLiteral("✗ ") + message);
        // 成功/失败色取 Fluent token（改版前取旧 ThemeColors 语义色）
        const FluentTokens t = fluentTheme(systemSchemeDark());
        m_testStatus->setStyleSheet(QStringLiteral("color: %1;")
                                        .arg(ok ? t.successText : t.errText));
    });
    connect(m_testBtn, &QPushButton::clicked, testCard, [this] {
        if (m_tester->busy())
            return;
        m_testBtn->setEnabled(false);
        m_testBtn->setText(QStringLiteral("测试中…"));
        m_testStatus->setText(QString());
        m_tester->test(m_urlEdit->text(), m_keyEdit->text(), m_modelEdit->text(),
                       apiFormatFromSetting(m_formatCombo->currentData().toString()));
    });
    auto *testRow = new QWidget(testCard);
    auto *testLay = new QHBoxLayout(testRow);
    testLay->setContentsMargins(0, 0, 0, 0);
    testLay->setSpacing(8);
    testLay->addWidget(m_testBtn);
    testLay->addWidget(m_testStatus, 1);
    testCard->setTrailingWidget(testRow);
    apiGroup->addCard(testCard);

    // ---- 翻译领域与术语 ----
    auto *termGroup = new SettingCardGroup(QStringLiteral("翻译领域与术语"), page);
    lay->addWidget(termGroup);

    auto *domainCard = new SettingCard(
        QStringLiteral("领域"),
        QStringLiteral("逗号分隔，注入提示词让模型按领域术语翻译，可留空"), page);
    m_domainEdit = new QLineEdit(Settings::instance().translateDomains().join(", "), domainCard);
    m_domainEdit->setPlaceholderText("如：医学, 法律, 计算机");
    m_domainEdit->setMinimumWidth(320);
    domainCard->setTrailingWidget(m_domainEdit);
    termGroup->addCard(domainCard);

    auto *termsCard = new SettingCard(
        QStringLiteral("专有名词翻译对"),
        QStringLiteral("每行一条：原文=译文，整体注入提示词强制按术语表翻译"), page);
    m_termsEdit = new QPlainTextEdit(termsCard);
    QString termLines;
    for (const auto &term : Settings::instance().translateTerms())
        termLines += term.first + "=" + term.second + "\n";
    m_termsEdit->setPlainText(termLines);
    m_termsEdit->setPlaceholderText(
        "每行一条：原文=译文\n如：\nworkflow=工作流\nmyocardial infarction=心肌梗死");
    m_termsEdit->setFixedHeight(88);
    m_termsEdit->setMinimumWidth(400);
    termsCard->setTrailingWidget(m_termsEdit);
    termGroup->addCard(termsCard);

    lay->addStretch();
    return page;
}

// 活动监控页：双开关（隐私影响全写在卡片描述，总开关默认关）+ 参数卡片组
QWidget *SettingsDialog::buildMonitorPage()
{
    auto *page = newPage(QStringLiteral("活动监控"));
    auto *lay = qobject_cast<QVBoxLayout *>(page->layout());
    Q_ASSERT(lay);

    auto *recordGroup = new SettingCardGroup(QStringLiteral("记录"), page);
    lay->addWidget(recordGroup);

    auto *enabledCard = new SettingCard(
        QStringLiteral("开启活动监控"),
        QStringLiteral("在本地记录你输入的文字与使用的窗口，供 Goody 管家生成"
                       "「本日工作记录」。数据只存本机，生成总结时当日摘要会"
                       "发送给所配的大模型 API。"), page);
    m_monEnabledSwitch = new ToggleSwitch(enabledCard);
    m_monEnabledSwitch->setChecked(Settings::instance().monitorEnabled());
    enabledCard->setTrailingWidget(m_monEnabledSwitch);
    recordGroup->addCard(enabledCard);

    auto *ocrCard = new SettingCard(
        QStringLiteral("中文上屏 OCR 快照"),
        QStringLiteral("全局键盘钩子拿不到输入法的上屏中文，只记得到拼音。开启后"
                       "检测到中文上屏（按空格/回车）会截取前台窗口做本地 OCR，"
                       "识别出新增文字后立即删除截图。"), page);
    m_monOcrSwitch = new ToggleSwitch(ocrCard);
    m_monOcrSwitch->setChecked(Settings::instance().monitorOcrSnapshot());
    ocrCard->setTrailingWidget(m_monOcrSwitch);
    recordGroup->addCard(ocrCard);

    auto *paramGroup = new SettingCardGroup(QStringLiteral("参数"), page);
    lay->addWidget(paramGroup);

    auto *idleCard = new SettingCard(QStringLiteral("判定离开的空闲时长"),
                                     QStringLiteral("键鼠无输入超过该时长即判定离开，工作时长不再累计"), page);
    m_monIdleSpin = new QSpinBox(idleCard);
    m_monIdleSpin->setRange(10, 3600);
    m_monIdleSpin->setSuffix(QStringLiteral(" 秒"));
    m_monIdleSpin->setValue(Settings::instance().monitorIdleThresholdSec());
    m_monIdleSpin->setMinimumWidth(130);
    idleCard->setTrailingWidget(m_monIdleSpin);
    paramGroup->addCard(idleCard);

    auto *retCard = new SettingCard(QStringLiteral("日志保留天数"),
                                    QStringLiteral("活动日志按天存放，超期目录在启动时自动删除"), page);
    m_monRetentionSpin = new QSpinBox(retCard);
    m_monRetentionSpin->setRange(1, 3650);
    m_monRetentionSpin->setSuffix(QStringLiteral(" 天"));
    m_monRetentionSpin->setValue(Settings::instance().monitorRetentionDays());
    m_monRetentionSpin->setMinimumWidth(130);
    retCard->setTrailingWidget(m_monRetentionSpin);
    paramGroup->addCard(retCard);

    lay->addWidget(makeHint(QStringLiteral(
        "日志位置：%LOCALAPPDATA%\\PopTools\\monitor\\<日期>\\（JSONL 文本，可用记事本打开）。\n"
        "托盘菜单可一键启停。")));
    lay->addStretch();
    return page;
}

// Goody 管家页：独立 API 卡片组 + 权限/每日记录 + 悬浮球 + 人格附言
QWidget *SettingsDialog::buildGoodyPage()
{
    auto *page = newPage(QStringLiteral("Goody 管家"));
    auto *lay = qobject_cast<QVBoxLayout *>(page->layout());
    Q_ASSERT(lay);

    // ---- 大模型 API ----
    auto *apiGroup = new SettingCardGroup(QStringLiteral("大模型 API（OpenAI 兼容）"), page);
    lay->addWidget(apiGroup);

    auto *keyCard = new SettingCard(QStringLiteral("API Key"),
                                    QStringLiteral("只保存在本机 PopTools.ini"), page);
    m_gdyKeyEdit = new QLineEdit(keyCard);
    m_gdyKeyEdit->setEchoMode(QLineEdit::Password);
    m_gdyKeyEdit->setPlaceholderText(QStringLiteral("sk-…"));
    m_gdyKeyEdit->setText(Settings::instance().assistantApiKey());
    m_gdyKeyEdit->setMinimumWidth(300);
    keyCard->setTrailingWidget(m_gdyKeyEdit);
    apiGroup->addCard(keyCard);

    auto *urlCard = new SettingCard(QStringLiteral("Base URL"), QString(), page);
    m_gdyUrlEdit = new QLineEdit(urlCard);
    m_gdyUrlEdit->setText(Settings::instance().assistantApiBaseUrl());
    m_gdyUrlEdit->setMinimumWidth(300);
    urlCard->setTrailingWidget(m_gdyUrlEdit);
    apiGroup->addCard(urlCard);

    auto *modelCard = new SettingCard(QStringLiteral("模型"),
                                      QStringLiteral("调试推荐 deepseek-v4-flash（便宜快）；正式总结可换 deepseek-chat"), page);
    m_gdyModelEdit = new QLineEdit(modelCard);
    m_gdyModelEdit->setText(Settings::instance().assistantApiModel());
    m_gdyModelEdit->setMinimumWidth(300);
    modelCard->setTrailingWidget(m_gdyModelEdit);
    apiGroup->addCard(modelCard);

    // ---- 权限与每日记录 ----
    auto *permGroup = new SettingCardGroup(QStringLiteral("权限与每日记录"), page);
    lay->addWidget(permGroup);

    auto *permCard = new SettingCard(QStringLiteral("权限模式"), QString(), page);
    m_gdyPermCombo = new QComboBox(permCard);
    m_gdyPermCombo->addItem(QStringLiteral("逐条确认（写文件/执行命令前都问我）"),
                            QStringLiteral("ask"));
    m_gdyPermCombo->addItem(QStringLiteral("智能自动（安全命令白名单内自动执行）"),
                            QStringLiteral("smart"));
    m_gdyPermCombo->addItem(QStringLiteral("全自动（不再询问，风险自负）"), QStringLiteral("auto"));
    const QString permMode = Settings::instance().assistantPermMode();
    for (int i = 0; i < m_gdyPermCombo->count(); ++i)
        if (m_gdyPermCombo->itemData(i).toString() == permMode)
            m_gdyPermCombo->setCurrentIndex(i);
    permCard->setTrailingWidget(m_gdyPermCombo);
    permGroup->addCard(permCard);

    auto *timeCard = new SettingCard(
        QStringLiteral("每日总结时刻"),
        QStringLiteral("每天到点后检查活动日志，为有记录但未总结的日子生成「本日工作记录」"), page);
    m_gdyTimeEdit = new QLineEdit(timeCard);
    m_gdyTimeEdit->setText(Settings::instance().assistantSummaryTime());
    m_gdyTimeEdit->setPlaceholderText(QStringLiteral("22:00"));
    m_gdyTimeEdit->setMinimumWidth(110);
    // 输入即校验 HH:mm（R3 #15）；Intermediate 残留（如“2:”）提交侧不落坏值
    //（见 commitSettings），读侧另有 22:00 回退兜底
    m_gdyTimeEdit->setValidator(
        new QRegularExpressionValidator(QRegularExpression(QStringLiteral(
            "^([01]?\\d|2[0-3]):[0-5]\\d$")), m_gdyTimeEdit));
    timeCard->setTrailingWidget(m_gdyTimeEdit);
    permGroup->addCard(timeCard);

    auto *autoCard = new SettingCard(QStringLiteral("自动生成每日工作记录"),
                                     QStringLiteral("启动时补账 + 每日定时"), page);
    m_gdyAutoSwitch = new ToggleSwitch(autoCard);
    m_gdyAutoSwitch->setChecked(Settings::instance().assistantAutoSummarize());
    autoCard->setTrailingWidget(m_gdyAutoSwitch);
    permGroup->addCard(autoCard);

    // ---- 桌面悬浮球（T-050） ----
    auto *ballGroup = new SettingCardGroup(QStringLiteral("桌面悬浮球"), page);
    lay->addWidget(ballGroup);
    auto *ballCard = new SettingCard(
        QStringLiteral("在桌面显示 Goody 悬浮球"),
        QStringLiteral("单击打开对话，可拖动并记忆位置；Goody 工作时呼吸光效提示"), page);
    m_gdyBallSwitch = new ToggleSwitch(ballCard);
    m_gdyBallSwitch->setChecked(Settings::instance().assistantBallEnabled());
    ballCard->setTrailingWidget(m_gdyBallSwitch);
    ballGroup->addCard(ballCard);

    // ---- 人格附言 ----
    auto *personaGroup = new SettingCardGroup(QStringLiteral("人格附言"), page);
    lay->addWidget(personaGroup);
    auto *personaCard = new SettingCard(QStringLiteral("追加内容"),
                                        QStringLiteral("追加到 Goody 系统提示词末尾，例如：称呼我为“老板”；记录风格口语化一点…"), page);
    m_gdyAppendix = new QPlainTextEdit(personaCard);
    m_gdyAppendix->setPlaceholderText(QStringLiteral("例如：称呼我为\"老板\"；记录风格口语化一点…"));
    m_gdyAppendix->setFixedHeight(80);
    m_gdyAppendix->setMinimumWidth(400);
    m_gdyAppendix->setPlainText(Settings::instance().assistantPersonaAppendix());
    personaCard->setTrailingWidget(m_gdyAppendix);
    personaGroup->addCard(personaCard);

    // ---- 打开对话窗（T-069b 菜单精简：托盘「Goody 管家」的等价入口） ----
    auto *openGroup = new SettingCardGroup(QStringLiteral("对话窗"), page);
    lay->addWidget(openGroup);
    auto *openCard = new SettingCard(
        QStringLiteral("打开 Goody 管家"),
        QStringLiteral("呼出方式：Ctrl+Alt+G、桌面悬浮球单击，或点右侧按钮"), page);
    auto *openBtn = new QPushButton(QStringLiteral("打开"), openCard);
    connect(openBtn, &QPushButton::clicked, this, [this] {
        if (m_services && m_services->goodyOpenChat)
            m_services->goodyOpenChat();
    });
    openCard->setTrailingWidget(openBtn);
    openGroup->addCard(openCard);

    lay->addWidget(makeHint(QStringLiteral(
        "Key 只保存在本机 PopTools.ini，不会随仓库提交。")));
    lay->addStretch();
    return page;
}

// 桌面日历页：读 CalendarSettings JSON。显示开关即时生效（load→改→save
// 保留未暴露字段，经 calendarApplySettings 通知宿主）；四个功能按钮经
// AppServices 其余四槽位即时生效
QWidget *SettingsDialog::buildCalendarPage()
{
    auto *page = newPage(QStringLiteral("桌面日历"));
    auto *lay = qobject_cast<QVBoxLayout *>(page->layout());
    Q_ASSERT(lay);

    // 桌面日历本体（注入式贴壁纸日历）上只有翻页与「调整位置和大小」按钮
    //（需要即时可视反馈），其余开关与功能入口全部收编到本页
    const CalendarSettings cal = CalendarSettings::load();

    // ---- 显示内容 ----
    auto *displayGroup = new SettingCardGroup(QStringLiteral("显示内容"), page);
    lay->addWidget(displayGroup);

    auto *lunarCard = new SettingCard(QStringLiteral("显示农历 / 节气 / 节日"), QString(), page);
    m_calLunarSwitch = new ToggleSwitch(lunarCard);
    m_calLunarSwitch->setChecked(cal.showLunar);
    lunarCard->setTrailingWidget(m_calLunarSwitch);
    displayGroup->addCard(lunarCard);

    auto *weekCard = new SettingCard(QStringLiteral("显示周数"), QString(), page);
    m_calWeekNumSwitch = new ToggleSwitch(weekCard);
    m_calWeekNumSwitch->setChecked(cal.showWeekNumbers);
    weekCard->setTrailingWidget(m_calWeekNumSwitch);
    displayGroup->addCard(weekCard);

    auto *eventsCard = new SettingCard(QStringLiteral("显示日程内容"), QString(), page);
    m_calEventsSwitch = new ToggleSwitch(eventsCard);
    m_calEventsSwitch->setChecked(cal.showEvents);
    eventsCard->setTrailingWidget(m_calEventsSwitch);
    displayGroup->addCard(eventsCard);

    auto *glassCard = new SettingCard(QStringLiteral("毛玻璃特效"),
                                      QStringLiteral("模糊壁纸背景"), page);
    m_calGlassSwitch = new ToggleSwitch(glassCard);
    m_calGlassSwitch->setChecked(cal.glassEffect);
    glassCard->setTrailingWidget(m_calGlassSwitch);
    displayGroup->addCard(glassCard);

    auto *listCard = new SettingCard(QStringLiteral("显示日历清单"),
                                     QStringLiteral("取消勾选即从桌面隐藏日历"), page);
    m_calListSwitch = new ToggleSwitch(listCard);
    m_calListSwitch->setChecked(!cal.hidden);
    listCard->setTrailingWidget(m_calListSwitch);
    displayGroup->addCard(listCard);

    auto *fontCard = new SettingCard(QStringLiteral("字体大小"),
                                     QStringLiteral("70%–160%，步进 5%"), page);
    m_calFontSpin = new QSpinBox(fontCard);
    m_calFontSpin->setRange(70, 160);
    m_calFontSpin->setSingleStep(5);
    // cal.fontScale 可能来自手改 JSON（CalendarSettings::fromJson 不钳制范围），
    // 此处 clamp 与消费端 CalendarRenderer.cpp:120 clamp 构成双重保险
    m_calFontSpin->setValue(std::clamp(cal.fontScale, 70, 160));
    m_calFontSpin->setMinimumWidth(130);
    m_calFontSpin->setSuffix(QStringLiteral("%"));
    fontCard->setTrailingWidget(m_calFontSpin);
    displayGroup->addCard(fontCard);

    // ---- 功能入口（即时生效） ----
    auto *actionGroup = new SettingCardGroup(QStringLiteral("功能入口"), page);
    lay->addWidget(actionGroup);

    // T-069b 菜单精简：桌面日历总开关（原托盘「停用桌面日历」，桌面表面
    // duty 语义，即时槽调用不经 commitSettings；停用态重启不记忆——与原
    // 托盘行为一致）
    auto *dutyCard = new SettingCard(
        QStringLiteral("启用桌面日历"),
        QStringLiteral("停用即整体撤下桌面日历（壁纸注入暂停）；与「显示日历清单」"
                       "开关独立——清单开关只在日历继续显示时生效"), page);
    m_calDutySwitch = new ToggleSwitch(dutyCard);
    m_calDutySwitch->setChecked(
        !(m_services && m_services->calendarDutyStopped && m_services->calendarDutyStopped()));
    connect(m_calDutySwitch, &QAbstractButton::toggled, this, [this](bool enable) {
        if (m_services && m_services->calendarSetDuty)
            m_services->calendarSetDuty(enable);
    });
    dutyCard->setTrailingWidget(m_calDutySwitch);
    actionGroup->addCard(dutyCard);

    auto *adjustCard = new SettingCard(
        QStringLiteral("调整桌面日历位置和大小"),
        QStringLiteral("进入调整模式：拖动虚线框定位，✓ 确认 / ✕ 取消"), page);
    auto *adjustBtn = new QPushButton(QStringLiteral("开始调整"), adjustCard);
    connect(adjustBtn, &QPushButton::clicked, this, [this] {
        if (m_services && m_services->calendarAdjust) {
            m_services->calendarAdjust();
            close(); // 调整模式全屏交互，设置对话框让路（与打开搜索等弹窗同理）
        }
    });
    adjustCard->setTrailingWidget(adjustBtn);
    actionGroup->addCard(adjustCard);

    // AppServices 四槽位为 std::function 成员（可能为空，单测/模块关停场景），
    // 单测场景 page 构建期 m_services 为空——槽位探活必须延迟到点击时
    auto makeAction = [this, page, actionGroup](const QString &title, const QString &desc,
                                                std::function<void(AppServices *)> invoke) {
        auto *card = new SettingCard(title, desc, page);
        auto *btn = new QPushButton(QStringLiteral("打开"), card);
        connect(btn, &QPushButton::clicked, this, [this, invoke] {
            if (!m_services)
                return;
            invoke(m_services);
        });
        card->setTrailingWidget(btn);
        actionGroup->addCard(card);
    };
    makeAction(QStringLiteral("内容搜索"), QStringLiteral("在全部日程中搜索文本"),
               [](AppServices *s) { if (s->calendarOpenSearch) s->calendarOpenSearch(); });
    makeAction(QStringLiteral("选择日期"), QStringLiteral("跳转到指定日期"),
               [](AppServices *s) { if (s->calendarPickDate) s->calendarPickDate(); });
    makeAction(QStringLiteral("导出日程"), QStringLiteral("把日程导出为文件"),
               [](AppServices *s) { if (s->calendarExportTasks) s->calendarExportTasks(); });
    makeAction(QStringLiteral("导入日程"), QStringLiteral("从文件导入日程"),
               [](AppServices *s) { if (s->calendarImportTasks) s->calendarImportTasks(); });

    // ---- 其他 ----
    auto *miscGroup = new SettingCardGroup(QStringLiteral("其他"), page);
    lay->addWidget(miscGroup);
    auto *guideCard = new SettingCard(QStringLiteral("显示首次使用提示"),
                                      QStringLiteral("调整位置入口气泡"), page);
    m_calGuideSwitch = new ToggleSwitch(guideCard);
    m_calGuideSwitch->setChecked(cal.showGuide);
    guideCard->setTrailingWidget(m_calGuideSwitch);
    miscGroup->addCard(guideCard);

    lay->addWidget(makeHint(QStringLiteral("数据目录：%1\n数据仅保存在本机，"
                                           "本页修改即时生效。")
                                .arg(CalendarSettings::dataDir())));
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

// 主题应用（构造末尾与 colorSchemeChanged 时调用）：Fluent token 全量重设。
// 布局类声明（ID 选择器）+ 控件基线（类型选择器，经通用 QSS 片段生成器）
void SettingsDialog::applyTheme()
{
    const FluentTokens t = fluentTheme(systemSchemeDark());
    // 多参 .arg 单趟替换，占位符 ≤9（%10 会被 %1 误吞，SettingsDialog 历史坑）
    setStyleSheet(QStringLiteral(
                      "#settingsDialog { background: %1; }"
                      "#settingsNav { background: %1; border-right: 1px solid %2; }"
                      "#navTitle { color: %3; font-size: 20px; font-weight: 600;"
                      "  padding: 6px 8px 8px 8px; }"
                      "QListWidget#settingsNavList { background: transparent; border: none;"
                      "  outline: 0; font-size: 14px; }"
                      "QListWidget#settingsNavList::item { color: %3; padding: 9px 14px;"
                      "  border-radius: 5px; margin: 1px 6px; }"
                      "QListWidget#settingsNavList::item:hover { background: %4; }"
                      "QListWidget#settingsNavList::item:selected { background: %5;"
                      "  color: %3; font-weight: 600; }"
                      "QStackedWidget#settingsPages { background: %1; }"
                      "QScrollArea#settingsPageScroll { background: %1; border: none; }"
                      "QWidget#settingsPage { background: %1; }"
                      "#settingsPageTitle { color: %3; font-size: 20px; font-weight: 600; }"
                      "#settingGroupTitle { color: %3; font-size: 14px; font-weight: 600;"
                      "  padding-left: 2px; }"
                      "QFrame#settingCardGroup { background: %6; border: 1px solid %7;"
                      "  border-radius: 8px; }"
                      "QWidget#settingCard { background: transparent; }"
                      "QWidget#settingCard:hover { background: %8; }"
                      "#settingCardTitle { color: %3; font-size: 14px; font-weight: 600;"
                      "  background: transparent; }"
                      "#settingCardDesc { color: %9; font-size: 12px;"
                      "  background: transparent; }")
                      .arg(t.bgBase, t.divider, t.textPrimary, t.bgSubtle, t.accentSoft,
                           t.bgCard, t.cardStroke, t.bgCardHover, t.textSecondary) +
                  QStringLiteral("QWidget#settingCardSeparator { background: %1; }"
                                 "#settingsHint { color: %2; font-size: 12px; }"
                                 "QToolButton { background: %3; border: 1px solid %4;"
                                 "  border-radius: 4px; padding: 4px 10px; color: %5; }"
                                 "QToolButton:hover { background: %6; }")
                      .arg(t.divider, t.textTertiary, t.ctrlBg, t.ctrlBorder,
                           t.textPrimary, t.ctrlHover) +
                  fluentInputQss(t) + fluentButtonQss(t) + fluentScrollBarQss(t));
    applyFluentWindowChrome(this, t.dark);
}

} // namespace poptools
