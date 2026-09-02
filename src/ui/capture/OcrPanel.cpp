#include "OcrPanel.h"

#include "core/settings/Settings.h"
#include "core/translation/Languages.h"
#include "ui/ThemeColors.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFrame>
#include <QGuiApplication>
#include <QStyleHints>
#include <QPalette>
#include <QLineEdit>
#include <QClipboard>
#include <QTimer>

namespace poptools {

OcrPanel::OcrPanel(QWidget *parent)
    : QWidget(parent)
{
    // 面板自身使用箭头光标
    setCursor(Qt::ArrowCursor);

    // 跟随系统明暗主题（本类在 poptools 命名空间，样式表类型选择器
    // "OcrPanel {...}" 匹配不到，须用 objectName ID 选择器）。
    // applyTheme() 在构造末尾调用——内含对子控件的调色板设置
    setObjectName("ocrPanel");
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, &OcrPanel::applyTheme);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // 标题行：OCR 标签 + 关闭按钮
    auto *headerRow = new QHBoxLayout;
    auto *titleLabel = new QLabel(QStringLiteral("OCR 识别"), this);
    headerRow->addWidget(titleLabel);
    headerRow->addStretch(1);
    m_closeBtn = new QPushButton(QStringLiteral("✕"), this);
    m_closeBtn->setObjectName("ocrCloseBtn");
    m_closeBtn->setFixedSize(20, 20);
    m_closeBtn->setToolTip(QStringLiteral("关闭面板，清除翻译缓存"));
    headerRow->addWidget(m_closeBtn);
    layout->addLayout(headerRow);

    // 上半部分：OCR 识别到的文字（只读，多行）
    // 不设占位提示文字：本应用没有用户输入场景，灰色提示字与约定不符
    m_ocrEdit = new QTextEdit(this);
    m_ocrEdit->setReadOnly(true);
    m_ocrEdit->setMinimumHeight(90);
    layout->addWidget(m_ocrEdit, 1);

    // 区段分隔线
    auto *sectionSep = new QFrame(this);
    sectionSep->setFrameShape(QFrame::HLine);
    layout->addWidget(sectionSep);

    // 中间：翻译选项（从 A 到 B）+ 翻译按钮
    auto *toolRow = new QHBoxLayout;
    toolRow->setSpacing(4);
    m_fromCombo = new QComboBox(this);
    m_fromCombo->addItems(languageDisplays());
    toolRow->addWidget(m_fromCombo);
    auto *arrow = new QLabel(QStringLiteral("→"), this);
    toolRow->addWidget(arrow);
    m_toCombo = new QComboBox(this);
    m_toCombo->setEditable(true);
    m_toCombo->addItems(languageDisplays().mid(1));
    // 恢复记忆的目标语言（用户上次切换的语言，划词卡片与文字栏共用）
    m_toCombo->setCurrentText(langCodeToDisplay(Settings::instance().translateTargetLang()));
    // 用户切换目标语言 → 立即记忆
    connect(m_toCombo, &QComboBox::currentTextChanged, this, [](const QString &display) {
        Settings::instance().setTranslateTargetLang(displayToLangCode(display));
    });
    toolRow->addWidget(m_toCombo, 1);
    m_translateBtn = new QPushButton(QStringLiteral("翻译"), this);
    m_translateBtn->setToolTip(QStringLiteral("将识别文字翻译为目标语言（需手动点击）"));
    toolRow->addWidget(m_translateBtn);
    layout->addLayout(toolRow);

    // 区段分隔线
    auto *sectionSep2 = new QFrame(this);
    sectionSep2->setFrameShape(QFrame::HLine);
    layout->addWidget(sectionSep2);

    // 下半部分：翻译结果（只读）
    m_translateEdit = new QTextEdit(this);
    m_translateEdit->setReadOnly(true);
    m_translateEdit->setMinimumHeight(60);
    layout->addWidget(m_translateEdit, 1);

    // 翻译结果下方：一键复制
    auto *copyRow = new QHBoxLayout;
    copyRow->addStretch(1);
    m_copyBtn = new QPushButton(QStringLiteral("复制译文"), this);
    m_copyBtn->setToolTip(QStringLiteral("复制翻译结果到剪贴板"));
    m_copyBtn->setEnabled(false);
    copyRow->addWidget(m_copyBtn);
    layout->addLayout(copyRow);

    // 翻译按钮：手动触发
    connect(m_translateBtn, &QPushButton::clicked, this, [this] {
        const QString text = m_ocrText.trimmed();
        if (!text.isEmpty())
            emit translateRequested(text, sourceLang(), targetLang());
    });

    // 复制按钮：把翻译结果写入剪贴板并短暂反馈
    connect(m_copyBtn, &QPushButton::clicked, this, [this] {
        const QString text = m_translateEdit->toPlainText();
        if (text.isEmpty())
            return;
        QGuiApplication::clipboard()->setText(text);
        m_copyBtn->setText(QStringLiteral("已复制"));
        QTimer::singleShot(1200, this, [this] {
            m_copyBtn->setText(QStringLiteral("复制译文"));
        });
    });

    // 关闭按钮：隐藏面板 + 清除内容 + 通知外部清理翻译缓存
    connect(m_closeBtn, &QPushButton::clicked, this, [this] {
        m_ocrText.clear();
        m_ocrEdit->clear();
        m_translateEdit->clear();
        m_copyBtn->setEnabled(false);
        hide();
        emit closed();
    });

    // 全部子控件就绪后应用主题（内含对编辑框/下拉的调色板设置）
    applyTheme();
}

void OcrPanel::applyTheme()
{
    const ThemeColors c = uiThemeColors(systemSchemeDark());
    // 文本框颜色直接写在控件自身：父级样式表的 QTextEdit color 在部分
    // Win10 渲染路径下不生效（见 TranslateCard 同款处理）
    const QString editSheet = QStringLiteral(
        "QTextEdit { color: %1; background: %2; border: 1px solid %3;"
        "  border-radius: 3px; padding: 4px; font-size: 13px; }"
        "QTextEdit:focus { border-color: #2B6FF2; }").arg(c.body, c.editBg, c.editBorder);
    m_ocrEdit->setStyleSheet(editSheet);
    m_translateEdit->setStyleSheet(editSheet);

    setStyleSheet(QString(
        "#ocrPanel { background: %1; border: 1px solid %2; border-radius: 4px; }"
        "QComboBox { background: %3; border: 1px solid %4; border-radius: 3px;"
        "  color: %5; padding: 2px 4px; }"
        "QComboBox:focus { border-color: #2B6FF2; }"
        "QComboBox QAbstractItemView { background: %3; color: %5; }"
        "QPushButton { background: %3; border: 1px solid #2B6FF2; border-radius: 3px;"
        "  color: #2B6FF2; padding: 2px 10px; font-size: 12px; }"
        "QPushButton:hover { background: %6; }"
        "QPushButton:disabled { color: %7; border-color: %4; background: %6; }"
        "QLabel { color: %5; font-size: 12px; }"
        "QFrame { color: %8; }"
        "QPushButton#ocrCloseBtn { background: transparent; border: none;"
        "  color: %9; font-size: 14px; }"
        "QPushButton#ocrCloseBtn:hover { color: %5; }")
        .arg(c.cardBg, c.cardBorder, c.editBg, c.editBorder, c.body,
             c.ctrlHover, c.ctrlDisabledText, c.line, c.closeText));

    // 与 TranslateCard 同理：把 Text 角色三个组都钉到主题文字色，
    // 内部背景钉 Base/Window，避免任何系统样式路径下 QSS 不生效
    const QBrush textBrush{QColor(c.body)};
    const QBrush editBgBrush{QColor(c.editBg)};
    for (QTextEdit *edit : {m_ocrEdit, m_translateEdit}) {
        QPalette pal = edit->palette();
        for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive,
                                                 QPalette::Disabled}) {
            pal.setBrush(group, QPalette::Text, textBrush);
            pal.setBrush(group, QPalette::HighlightedText, textBrush);
            pal.setBrush(group, QPalette::Base, editBgBrush);
            pal.setBrush(group, QPalette::Window, editBgBrush);
        }
        edit->setPalette(pal);
        edit->setAutoFillBackground(true);
    }
    for (QComboBox *combo : {m_fromCombo, m_toCombo}) {
        QPalette pal = combo->palette();
        for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive,
                                                 QPalette::Disabled}) {
            pal.setBrush(group, QPalette::Text, textBrush);
            pal.setBrush(group, QPalette::ButtonText, textBrush);
            pal.setBrush(group, QPalette::Base, editBgBrush);
        }
        combo->setPalette(pal);
        if (QLineEdit *line = combo->lineEdit()) // 可编辑下拉的内嵌输入框
            line->setPalette(pal);
    }
}

void OcrPanel::setOcrText(const QString &text)
{
    m_ocrText = text;
    m_ocrEdit->setPlainText(text);
}

void OcrPanel::setTranslateText(const QString &text)
{
    m_translateEdit->setPlainText(text);
    m_copyBtn->setEnabled(!text.isEmpty());
}

QString OcrPanel::sourceLang() const
{
    return displayToLangCode(m_fromCombo->currentText());
}

QString OcrPanel::targetLang() const
{
    return displayToLangCode(m_toCombo->currentText());
}

void OcrPanel::setBusy(bool busy)
{
    m_translateBtn->setDisabled(busy);
    m_translateBtn->setText(busy ? QStringLiteral("翻译中…") : QStringLiteral("翻译"));
    // 翻译进行中不允许复制（避免复制到"翻译中…"占位文本）
    m_copyBtn->setEnabled(!busy && !m_translateEdit->toPlainText().isEmpty());
}

// 面板每次显示（新截图翻译任务）源语言回到"自动检测"：只有目标语言做记忆，
// 源语言交给模型自动识别（面板打开期间用户仍可手动指定）
void OcrPanel::showEvent(QShowEvent *event)
{
    m_fromCombo->setCurrentIndex(0);
    QWidget::showEvent(event);
}

} // namespace poptools