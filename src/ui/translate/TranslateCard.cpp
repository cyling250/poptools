#include "TranslateCard.h"

#include "core/settings/Settings.h"
#include "core/translation/Languages.h"

#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QClipboard>
#include <QTimer>
#include <QtMath>

namespace poptools {

namespace {

// 距窗口边缘该距离内按下即进入调整大小（px），需小于布局边距以免盖住子控件
constexpr int kResizeMargin = 6;
// 文本区按内容测高的附加高度：上下内边距(8*2) + 边框(1*2) + 余量
constexpr int kTextEditChrome = 24;
// 文本区高度下限与自动模式上限（超出上限走滚动条）
constexpr int kTextEditMinHeight = 26;
constexpr int kSourceMaxHeight = 200;
constexpr int kTranslateMaxHeight = 340;
// 自动模式下的默认宽度（原固定宽 380）
constexpr int kDefaultWidth = 380;

// 命中窗口边缘（用于拖拽调整大小）
Qt::Edges edgesAt(const QPoint &p, const QSize &size)
{
    Qt::Edges e;
    if (p.x() <= kResizeMargin)
        e |= Qt::LeftEdge;
    if (p.x() >= size.width() - kResizeMargin)
        e |= Qt::RightEdge;
    if (p.y() <= kResizeMargin)
        e |= Qt::TopEdge;
    if (p.y() >= size.height() - kResizeMargin)
        e |= Qt::BottomEdge;
    return e;
}

Qt::CursorShape cursorForEdges(Qt::Edges e)
{
    if (e == (Qt::LeftEdge | Qt::TopEdge) || e == (Qt::RightEdge | Qt::BottomEdge))
        return Qt::SizeFDiagCursor;
    if (e == (Qt::TopEdge | Qt::RightEdge) || e == (Qt::BottomEdge | Qt::LeftEdge))
        return Qt::SizeBDiagCursor;
    if (e & (Qt::LeftEdge | Qt::RightEdge))
        return Qt::SizeHorCursor;
    if (e & (Qt::TopEdge | Qt::BottomEdge))
        return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

} // namespace

TranslateCard::TranslateCard(QWidget *parent)
    : QWidget(parent)
{
    // 无边框顶层窗口，不抢占焦点；全局单例复用，关闭即隐藏（hideEvent 清空内容）
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setCursor(Qt::ArrowCursor);
    setMinimumSize(320, 180);
    resize(kDefaultWidth, 200);
    // 未按下时也接收 mouseMove，用于边缘缩放的光标反馈
    setMouseTracking(true);

    // === 暗色主题 ===
    setStyleSheet(
        "TranslateCard { background: #1E1E1E; border: 1px solid #3C3C3C;"
        "  border-radius: 8px; }"
        "QLabel.section-title { color: #8E8E93; font-size: 11px; }"
        "QTextEdit { color: #CCCCCC; font-size: 13px;"
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

    // === 原文区域（只读文本框：内容过长时内部滚动，可随窗口拖拽扩大） ===
    auto *srcTitle = new QLabel(QStringLiteral("原文"), this);
    srcTitle->setProperty("class", "section-title");
    layout->addWidget(srcTitle);

    auto makeTextEdit = [this] {
        auto *edit = new QTextEdit(this);
        edit->setReadOnly(true);
        edit->setTextInteractionFlags(Qt::TextSelectableByMouse);
        edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        return edit;
    };
    m_sourceText = makeTextEdit();
    layout->addWidget(m_sourceText, 2);

    // === 分隔线 ===
    auto *sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::HLine);
    sep2->setStyleSheet("QFrame { color: #2A2A2A; }");
    layout->addWidget(sep2);

    // === 翻译区域 ===
    auto *trTitle = new QLabel(QStringLiteral("翻译"), this);
    trTitle->setProperty("class", "section-title");
    layout->addWidget(trTitle);

    m_translateText = makeTextEdit();
    layout->addWidget(m_translateText, 3);

    // === 复制行：翻译结果右下方一键复制 ===
    auto *copyRow = new QHBoxLayout;
    copyRow->addStretch(1);
    m_copyBtn = new QPushButton(QStringLiteral("复制译文"), this);
    m_copyBtn->setProperty("class", "ctrl-btn");
    m_copyBtn->setToolTip(QStringLiteral("复制翻译结果到剪贴板"));
    m_copyBtn->setEnabled(false);
    copyRow->addWidget(m_copyBtn);
    layout->addLayout(copyRow);

    // === 信号连接 ===
    connect(m_closeBtn, &QPushButton::clicked, this, [this] {
        emit closed();
        close();
    });
    connect(m_swapBtn, &QPushButton::clicked, this, &TranslateCard::onSwapLanguages);
    connect(m_translateBtn, &QPushButton::clicked, this, &TranslateCard::onTranslateClicked);
    connect(m_copyBtn, &QPushButton::clicked, this, [this] {
        const QString text = m_translateText->toPlainText();
        if (text.isEmpty())
            return;
        QGuiApplication::clipboard()->setText(text);
        m_copyBtn->setText(QStringLiteral("已复制"));
        QTimer::singleShot(1200, this, [this] {
            m_copyBtn->setText(QStringLiteral("复制译文"));
        });
    });
}

void TranslateCard::setSourceText(const QString &text)
{
    m_originalText = text;
    m_sourceText->setPlainText(text);
    refit();
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
    m_translateText->setPlainText(text);
    m_translateText->setStyleSheet(QString()); // 清除错误态配色，恢复默认
    m_translateBtn->setEnabled(true);
    m_translateBtn->setText(QStringLiteral("翻译"));
    m_copyBtn->setEnabled(!text.isEmpty());
    refit();
}

void TranslateCard::showError(const QString &errorMsg)
{
    m_translateText->setPlainText(errorMsg);
    m_translateText->setStyleSheet(
        "QTextEdit { color: #FF6B6B; font-size: 13px;"
        "  background: #2A0000; border: 1px solid #5C0000; border-radius: 4px;"
        "  padding: 8px; }");
    m_translateBtn->setEnabled(true);
    m_translateBtn->setText(QStringLiteral("翻译"));
    m_copyBtn->setEnabled(false);
    refit();
}

void TranslateCard::refit()
{
    if (m_userResized)
        return;

    // 先按目标宽度激活布局，文本区拿到实际宽度后按内容测高才准确
    resize(qMax(kDefaultWidth, layout()->totalMinimumSize().width()), 200);
    layout()->activate();

    fitTextEdit(m_sourceText, kSourceMaxHeight);
    fitTextEdit(m_translateText, kTranslateMaxHeight);

    resize(width(), layout()->totalSizeHint().height());
    // 兜底：整体高度不超过当前屏幕可用高度的 85%
    if (const QScreen *screen = QGuiApplication::screenAt(QCursor::pos())) {
        const int maxCardH = qRound(screen->availableGeometry().height() * 0.85);
        if (height() > maxCardH)
            resize(width(), maxCardH);
    }
}

void TranslateCard::fitTextEdit(QTextEdit *edit, int maxH)
{
    const int docH = qCeil(edit->document()->size().height());
    const bool overflow = docH + kTextEditChrome > maxH;
    edit->setFixedHeight(overflow ? maxH : docH + kTextEditChrome);
    // 恰好容纳时关掉滚动条避免闪现；超出上限时启用滚动查看全文
    edit->setVerticalScrollBarPolicy(overflow ? Qt::ScrollBarAsNeeded
                                              : Qt::ScrollBarAlwaysOff);
}

void TranslateCard::beginUserResize()
{
    if (m_userResized)
        return;
    m_userResized = true;
    // 解除固定高度，交由布局按拉伸系数（原文 2 : 译文 3）分配窗口空间
    for (QTextEdit *edit : {m_sourceText, m_translateText}) {
        edit->setMinimumHeight(kTextEditMinHeight);
        edit->setMaximumHeight(QWIDGETSIZE_MAX);
        edit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
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
    m_translateText->setStyleSheet(QString());
    m_translateText->clear();
    m_copyBtn->setEnabled(false);
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
        m_resizeEdges = edgesAt(event->pos(), size());
        if (m_resizeEdges) {
            beginUserResize();
            m_pressGlobalPos = event->globalPos();
            m_pressGeometry = geometry();
        } else {
            m_dragOffset = event->pos();
            m_dragging = true;
        }
    }
    QWidget::mousePressEvent(event);
}

void TranslateCard::mouseMoveEvent(QMouseEvent *event)
{
    if (m_resizeEdges) {
        // 按边缘拖拽调整大小：右/下改尺寸，左/上同时改位置，尺寸不小于最小值
        const QPoint delta = event->globalPos() - m_pressGlobalPos;
        QRect g = m_pressGeometry;
        if (m_resizeEdges & Qt::RightEdge)
            g.setWidth(qMax(minimumWidth(), m_pressGeometry.width() + delta.x()));
        if (m_resizeEdges & Qt::BottomEdge)
            g.setHeight(qMax(minimumHeight(), m_pressGeometry.height() + delta.y()));
        if (m_resizeEdges & Qt::LeftEdge) {
            const int w = qMax(minimumWidth(), m_pressGeometry.width() - delta.x());
            g.setX(m_pressGeometry.right() - w + 1);
            g.setWidth(w);
        }
        if (m_resizeEdges & Qt::TopEdge) {
            const int h = qMax(minimumHeight(), m_pressGeometry.height() - delta.y());
            g.setY(m_pressGeometry.bottom() - h + 1);
            g.setHeight(h);
        }
        setGeometry(g);
    } else if (m_dragging) {
        move(event->globalPos() - m_dragOffset);
    } else {
        // 悬停在边缘时给出缩放光标提示
        setCursor(cursorForEdges(edgesAt(event->pos(), size())));
    }
    QWidget::mouseMoveEvent(event);
}

void TranslateCard::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        m_resizeEdges = Qt::Edges();
        setCursor(Qt::ArrowCursor);
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

void TranslateCard::hideEvent(QHideEvent *event)
{
    // 单例复用：隐藏即清空，覆盖所有退出路径（点击卡片外部 / 关闭按钮 / Esc），
    // 下次显示时是一张干净的卡片。语言选择与用户调整过的尺寸不重置（属记忆项）。
    m_originalText.clear();
    m_sourceText->clear();
    m_translateText->clear();
    m_translateText->setStyleSheet(QString()); // 清除错误态配色
    m_translateBtn->setEnabled(true);
    m_translateBtn->setText(QStringLiteral("翻译"));
    m_copyBtn->setEnabled(false);
    m_copyBtn->setText(QStringLiteral("复制译文"));
    QWidget::hideEvent(event);
}

} // namespace poptools
