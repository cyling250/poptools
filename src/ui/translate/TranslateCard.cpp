#include "TranslateCard.h"

#include "core/settings/Settings.h"
#include "core/translation/Languages.h"

#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QClipboard>

namespace snipaste {

TranslateCard::TranslateCard(QWidget *parent)
    : QWidget(parent)
{
    // 无边框顶层窗口，不抢占焦点，自动关闭
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose);
    setCursor(Qt::ArrowCursor);
    setFixedWidth(380);

    // === 暗色主题 ===
    setStyleSheet(
        "TranslateCard { background: #1E1E1E; border: 1px solid #3C3C3C;"
        "  border-radius: 8px; }"
        "QLabel.section-title { color: #8E8E93; font-size: 11px; }"
        "QLabel.content-text { color: #CCCCCC; font-size: 13px;"
        "  background: #0D0D0D; border: 1px solid #2A2A2A; border-radius: 4px;"
        "  padding: 8px; }"
        "QComboBox { background: #2A2A2A; color: #CCCCCC; border: 1px solid #3C3C3C;"
        "  border-radius: 3px; padding: 2px 4px; font-size: 12px; }"
        "QComboBox:focus { border-color: #2B6FF2; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #2A2A2A; color: #CCCCCC;"
        "  selection-background-color: #3C3C3C; }"
        "QPushButton.ctrl-btn { background: #2A2A2A; color: #CCCCCC;"
        "  border: 1px solid #3C3C3C; border-radius: 3px; padding: 2px 6px;"
        "  font-size: 12px; }"
        "QPushButton.ctrl-btn:hover { background: #3C3C3C; }"
        "QPushButton.translate-btn { background: #2B6FF2; color: #FFFFFF;"
        "  border: none; border-radius: 3px; padding: 2px 10px; font-size: 12px; }"
        "QPushButton.translate-btn:hover { background: #1E5FD9; }"
        "QPushButton.translate-btn:disabled { background: #3C3C3C; color: #666; }");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(6);

    // === 标题行：左侧文字 + 右侧关闭按钮 ===
    auto *headerRow = new QHBoxLayout;
    auto *titleLabel = new QLabel(QStringLiteral("划词翻译"), this);
    titleLabel->setStyleSheet("QLabel { color: #FFFFFF; font-weight: bold; font-size: 13px; }");
    headerRow->addWidget(titleLabel);
    headerRow->addStretch(1);
    m_closeBtn = new QPushButton(QStringLiteral("\303\227"), this);
    m_closeBtn->setFixedSize(22, 22);
    m_closeBtn->setToolTip(QStringLiteral("关闭"));
    m_closeBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; color: #8E8E93; font-size: 14px; }"
        "QPushButton:hover { color: #FFFFFF; }");
    headerRow->addWidget(m_closeBtn);
    layout->addLayout(headerRow);

    // === 语言控件行：源语言 ⇄ 目标语言 + 翻译按钮 ===
    auto *langRow = new QHBoxLayout;
    langRow->setSpacing(4);

    m_fromCombo = new QComboBox(this);
    m_fromCombo->addItems(languageDisplays());
    m_fromCombo->setCurrentIndex(0); // 默认自动
    langRow->addWidget(m_fromCombo);

    m_swapBtn = new QPushButton(QStringLiteral("\342\207\204"), this); // ⇄
    m_swapBtn->setFixedSize(26, 24);
    m_swapBtn->setToolTip(QStringLiteral("切换源语言和目标语言"));
    m_swapBtn->setProperty("class", "ctrl-btn");
    langRow->addWidget(m_swapBtn);

    m_toCombo = new QComboBox(this);
    m_toCombo->addItems(languageDisplays().mid(1)); // 不含"自动"
    // 恢复记忆的目标语言（用户上次切换的语言），而不是每次都回到默认中文；
    // 恢复值不在列表中（非可编辑下拉 setCurrentText 未命中）时保持默认中文
    m_toCombo->setCurrentText(langCodeToDisplay(Settings::instance().translateTargetLang()));
    // 用户切换目标语言 → 立即记忆（划词卡片与 OCR 文字栏共用该设置）
    connect(m_toCombo, &QComboBox::currentTextChanged, this, [](const QString &display) {
        Settings::instance().setTranslateTargetLang(displayToLangCode(display));
    });
    langRow->addWidget(m_toCombo, 1);

    m_translateBtn = new QPushButton(QStringLiteral("翻译"), this);
    m_translateBtn->setProperty("class", "translate-btn");
    m_translateBtn->setToolTip(QStringLiteral("开始翻译"));
    langRow->addWidget(m_translateBtn);

    layout->addLayout(langRow);

    // === 分隔线 ===
    auto *sep1 = new QFrame(this);
    sep1->setFrameShape(QFrame::HLine);
    sep1->setStyleSheet("QFrame { color: #2A2A2A; }");
    layout->addWidget(sep1);

    // === 原文区域 ===
    auto *srcTitle = new QLabel(QStringLiteral("原文"), this);
    srcTitle->setProperty("class", "section-title");
    layout->addWidget(srcTitle);

    m_sourceText = new QLabel(this);
    m_sourceText->setWordWrap(true);
    m_sourceText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_sourceText->setMinimumHeight(24);
    m_sourceText->setMaximumHeight(120);
    m_sourceText->setProperty("class", "content-text");
    layout->addWidget(m_sourceText);

    // === 分隔线 ===
    auto *sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::HLine);
    sep2->setStyleSheet("QFrame { color: #2A2A2A; }");
    layout->addWidget(sep2);

    // === 翻译区域 ===
    auto *trTitle = new QLabel(QStringLiteral("翻译"), this);
    trTitle->setProperty("class", "section-title");
    layout->addWidget(trTitle);

    m_translateText = new QLabel(this);
    m_translateText->setWordWrap(true);
    m_translateText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_translateText->setMinimumHeight(24);
    m_translateText->setMaximumHeight(200);
    m_translateText->setProperty("class", "content-text");
    layout->addWidget(m_translateText);

    // === 信号连接 ===
    connect(m_closeBtn, &QPushButton::clicked, this, [this] {
        emit closed();
        close();
    });
    connect(m_swapBtn, &QPushButton::clicked, this, &TranslateCard::onSwapLanguages);
    connect(m_translateBtn, &QPushButton::clicked, this, &TranslateCard::onTranslateClicked);
}

void TranslateCard::setSourceText(const QString &text)
{
    m_originalText = text;
    m_sourceText->setText(text);
    adjustSize();
    // 定位到鼠标位置
    moveNearCursor();
}

void TranslateCard::moveNearCursor()
{
    const QRect screenGeo = QGuiApplication::screenAt(QCursor::pos())->availableGeometry();
    QPoint pos = QCursor::pos() + QPoint(12, 12);
    if (pos.x() + width() > screenGeo.right())
        pos.setX(screenGeo.right() - width());
    if (pos.y() + height() > screenGeo.bottom())
        pos.setY(screenGeo.bottom() - height());
    move(pos);
}

void TranslateCard::setTranslateResult(const QString &text)
{
    m_translateText->setText(text);
    m_translateText->setStyleSheet(
        "QLabel { color: #CCCCCC; font-size: 13px;"
        "  background: #0D0D0D; border: 1px solid #2A2A2A; border-radius: 4px;"
        "  padding: 8px; }");
    m_translateBtn->setEnabled(true);
    m_translateBtn->setText(QStringLiteral("翻译"));
    adjustSize();
}

void TranslateCard::showError(const QString &errorMsg)
{
    m_translateText->setText(errorMsg);
    m_translateText->setStyleSheet(
        "QLabel { color: #FF6B6B; font-size: 13px;"
        "  background: #2A0000; border: 1px solid #5C0000; border-radius: 4px;"
        "  padding: 8px; }");
    m_translateBtn->setEnabled(true);
    m_translateBtn->setText(QStringLiteral("翻译"));
    adjustSize();
}

void TranslateCard::onSwapLanguages()
{
    const int fromIdx = m_fromCombo->currentIndex();
    const QString toText = m_toCombo->currentText();
    // 源语言不能是"自动"以外的，目标语言不能是"自动"
    if (fromIdx == 0) {
        // 源语言是"自动"，把目标语言切到源语言位，目标语言设为"自动"
        m_fromCombo->setCurrentText(toText);
        m_toCombo->setCurrentIndex(0); // "自动"在目标语言 combo 中不显示，设第一项
    } else {
        const QString fromText = m_fromCombo->currentText();
        m_fromCombo->setCurrentText(toText);
        m_toCombo->setCurrentText(fromText);
    }
}

void TranslateCard::translateNow()
{
    if (m_originalText.trimmed().isEmpty())
        return;
    m_translateBtn->setEnabled(false);
    m_translateBtn->setText(QStringLiteral("翻译中…"));
    m_translateText->setText(QString());
    emit translateRequested(m_originalText, displayToLangCode(m_fromCombo->currentText()),
                            displayToLangCode(m_toCombo->currentText()));
}

void TranslateCard::onTranslateClicked()
{
    translateNow();
}

void TranslateCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragOffset = event->pos();
        m_dragging = true;
    }
    QWidget::mousePressEvent(event);
}

void TranslateCard::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        move(event->globalPos() - m_dragOffset);
    }
    QWidget::mouseMoveEvent(event);
}

void TranslateCard::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void TranslateCard::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        emit closed();
        close();
    }
    QWidget::keyPressEvent(event);
}

} // namespace snipaste