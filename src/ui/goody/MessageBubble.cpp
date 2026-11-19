#include "GoodyTheme.h"
#include "MessageBubble.h"
// T-050-P2：消息气泡组件实现——user 贴纸 / assistant 卡片（思考折叠区 +
// 流式纯文本追加 + 一次 setMarkdown 定稿 + 代码块复制钮 + hover 四件套）；
// 关键颜色双通道见 applyTheme（P-022/P-023）

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFormat>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "CodeHighlighter.h"

#include <cmath>

namespace poptools {

namespace {
constexpr int kCopiedResetMs = 1200; // 代码复制按钮"已复制"反馈复位时长
                                     //（T-062 改名：原名 kStreamTickIntervalMs
                                     // 是旧流式实现的历史遗留，名不副实）

// 打字光标（流式期间缀在正文末尾，定稿时随 setMarkdown 一并消失）
const QString kCaret = QStringLiteral("▍");

constexpr int kInnerFollowSlackPx = 24; // T-050-FIX2：内滚"贴底才跟随"判定余量
} // namespace

// ============================================================
// AutoHeightBrowser
// ============================================================

AutoHeightBrowser::AutoHeightBrowser(QWidget *parent)
    : QTextBrowser(parent)
{
    setFrameShape(QFrame::NoFrame);
    setLineWrapMode(QTextEdit::WidgetWidth);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setOpenExternalLinks(true);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    // 文档尺寸变化（追加/换行重排）→ 高度跟随；QAbstractTextDocumentLayout
    // 信号，非定时器，无常驻开销
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
            this, [this] { adjustHeight(); });
}

void AutoHeightBrowser::adjustHeight()
{
    int h = int(std::ceil(document()->size().height())) + 2 * frameWidth() + 2;
    h = qBound(24, h, m_heightCap);
    if (h != height())
        setFixedHeight(h); // 高度稳定后不再触发（防 resize 循环）
}

void AutoHeightBrowser::resizeEvent(QResizeEvent *event)
{
    QTextBrowser::resizeEvent(event);
    adjustHeight(); // 宽度变化 → 换行重排 → 高度跟随
}

// ============================================================
// MessageBubble
// ============================================================

MessageBubble::MessageBubble(Role role, QWidget *parent)
    : QFrame(parent)
    , m_role(role)
{
    setAttribute(Qt::WA_StyledBackground, true); // QFrame 子类吃 ID 选择器背景
    m_lay = new QVBoxLayout(this);
    m_lay->setContentsMargins(0, 0, 0, 0);
    m_lay->setSpacing(4);
    switch (role) {
    case User:
        buildUser();
        break;
    case Assistant:
        buildAssistant();
        break;
    case System:
        buildSystem();
        break;
    }
}

// user：右对齐贴纸 QLabel + hover 工具条（复制/编辑重发/删除）
void MessageBubble::buildUser()
{
    setObjectName("goodyBubbleUser");
    // 右对齐贴纸式气泡：水平 Maximum（抱住内容），上限由窗口缩放时
    // setMaxContentWidth 收口
    setSizePolicy(QSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred));

    m_userText = new QLabel(this);
    m_userText->setObjectName("goodyUserText");
    m_userText->setWordWrap(true);
    m_userText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_userText->setContentsMargins(10, 7, 10, 7);
    m_lay->addWidget(m_userText);

    // 悬浮工具条（overlay，构建样板与 assistant 共用，见 makeToolbar）
    makeToolbar({{&m_btnCopy, int(BubbleCopy)},
                 {&m_btnEdit, int(BubbleEditResend)},
                 {&m_btnDelete, int(BubbleDelete)}});
    m_btnCopy->setText(QStringLiteral("复制"));
    m_btnEdit->setText(QStringLiteral("编辑重发"));
    m_btnDelete->setText(QStringLiteral("删除"));

    // hover 追踪：给自身与全部后代装 Enter/Leave 计数（离开到子控件不误判）
    installEventFilter(this);
    for (QWidget *w : findChildren<QWidget *>())
        w->installEventFilter(this);
}

// assistant：思考折叠区 + AutoHeightBrowser 正文 + 代码复制钮 + hover 工具条
//（复制/重新生成/删除）+ 围栏块语法高亮器
void MessageBubble::buildAssistant()
{
    setObjectName("goodyBubbleAssistant");
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setSizePolicy(sp);

    // ---- 思考折叠区（默认隐藏：首条 reasoning 增量到达即现，仍折叠） ----
    m_reasonSection = new QWidget(this);
    m_reasonSection->setObjectName("goodyReasonSection");
    auto *rsLay = new QVBoxLayout(m_reasonSection);
    rsLay->setContentsMargins(0, 0, 0, 0);
    rsLay->setSpacing(2);
    m_reasonHead = new QToolButton(m_reasonSection);
    m_reasonHead->setObjectName("goodyReasoningHead");
    m_reasonHead->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_reasonHead->setCheckable(true);
    m_reasonHead->setArrowType(Qt::RightArrow);
    m_reasonHead->setText(QStringLiteral("思考过程"));
    m_reasonBody = new QTextBrowser(m_reasonSection);
    m_reasonBody->setObjectName("goodyReasoningBody");
    m_reasonBody->setFrameShape(QFrame::NoFrame);
    m_reasonBody->setOpenExternalLinks(false);
    m_reasonBody->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_reasonBody->setFixedHeight(120); // 展开区内部滚动，不撑爆列表
    m_reasonBody->hide();
    rsLay->addWidget(m_reasonHead);
    rsLay->addWidget(m_reasonBody);
    m_reasonSection->hide();
    m_lay->addWidget(m_reasonSection);
    connect(m_reasonHead, &QToolButton::clicked, this, [this](bool checked) {
        m_reasonHead->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        m_reasonBody->setVisible(checked);
    });

    // ---- 正文 ----
    m_body = new AutoHeightBrowser(this);
    m_body->setObjectName("goodyBubbleText");
    m_lay->addWidget(m_body);
    // 语法高亮器挂文档：setMarkdown 后围栏块自动着色（finalize 专用）
    m_highlighter = new GoodyCodeHighlighter(m_body->document());

    // ---- 代码块复制钮（viewport 悬浮） ----
    m_codeCopyBtn = new QToolButton(m_body->viewport());
    m_codeCopyBtn->setObjectName("goodyCodeCopyBtn");
    m_codeCopyBtn->setText(QStringLiteral("复制"));
    m_codeCopyBtn->hide();
    connect(m_codeCopyBtn, &QToolButton::clicked, this, [this] {
        const QPoint pos = m_codeCopyBtn->mapTo(m_body->viewport(), QPoint(0, 0))
                           + QPoint(m_codeCopyBtn->width() / 2, 2);
        const QTextBlock block = codeBlockAtViewport(pos);
        if (block.isValid())
            copyCodeBlockUnder(block);
    });
    m_body->viewport()->setMouseTracking(true); // 无按键也要 MouseMove
    m_body->viewport()->installEventFilter(this);

    // ---- 悬浮工具条（overlay，构建样板与 user 共用，见 makeToolbar） ----
    makeToolbar({{&m_btnCopy, int(BubbleCopy)},
                 {&m_btnRegen, int(BubbleRegenerate)},
                 {&m_btnDelete, int(BubbleDelete)}});
    m_btnCopy->setText(QStringLiteral("复制"));
    m_btnRegen->setText(QStringLiteral("重新生成"));
    m_btnDelete->setText(QStringLiteral("删除"));

    installEventFilter(this);
    for (QWidget *w : findChildren<QWidget *>())
        w->installEventFilter(this);
}

// system：单行 QLabel 提示行（welcome/工具行/错误行）
void MessageBubble::buildSystem()
{
    setObjectName("goodyBubbleSystem");
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setSizePolicy(sp);
    m_systemText = new QLabel(this);
    m_systemText->setObjectName("goodySystemText");
    m_systemText->setWordWrap(true);
    m_systemText->setContentsMargins(6, 1, 6, 1);
    m_lay->addWidget(m_systemText);
}

// 悬浮工具条构建样板（T-062 抽取）：QFrame overlay + 文本按钮 + clicked→
// actionRequested 接线。原 buildUser/buildAssistant 两段约 30 行的逐行重复
// 构建合并于此；构建顺序、父子关系、objectName、connect 目标均与原实现一致
QFrame *MessageBubble::makeToolbar(std::initializer_list<std::pair<QToolButton **, int>> buttons)
{
    m_toolbar = new QFrame(this);
    m_toolbar->setObjectName("goodyBubbleToolbar");
    auto *tbLay = new QHBoxLayout(m_toolbar);
    tbLay->setContentsMargins(3, 2, 3, 2);
    tbLay->setSpacing(2);
    for (const auto &spec : buttons) {
        auto *b = new QToolButton(m_toolbar);
        b->setObjectName("goodyBubbleActBtn");
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        tbLay->addWidget(b);
        *spec.first = b;
        const int action = spec.second;
        connect(b, &QToolButton::clicked, this, [this, action] {
            emit actionRequested(action);
        });
    }
    m_toolbar->hide();
    return m_toolbar;
}

// ---- 内容 ----

void MessageBubble::setUserText(const QString &text)
{
    if (m_userText)
        m_userText->setText(text);
}

void MessageBubble::setSystemText(const QString &text, bool error)
{
    if (m_systemText)
        m_systemText->setText(text);
    if (error)
        setObjectName("goodyBubbleSystemError");
}

void MessageBubble::beginStream()
{
    if (!m_body)
        return;
    m_streaming = true;
    m_caretShown = false;
    m_body->clear();
    updateToolbar(); // 流式气泡不露工具条
}

void MessageBubble::appendStreamText(const QString &delta)
{
    if (!m_body || delta.isEmpty())
        return;
    // T-050-FIX2：内滚"贴底才跟随"（参照外层 GoodyWindow m_atBottom 的
    // value+page >= max - slack 双检风格）——追加前判定；未超 420px 高度
    // 上限（无内滚条）时条件恒成立，跟随行为不变。超限后用户上翻阅读
    // 不再被每个增量拽回底部
    QScrollBar *vsb = m_body->verticalScrollBar();
    const bool follow =
        vsb->value() + vsb->pageStep() >= vsb->maximum() - kInnerFollowSlackPx;
    QTextCursor c = m_body->textCursor();
    c.movePosition(QTextCursor::End);
    if (m_caretShown) {
        c.deletePreviousChar(); // 摘下旧光标再追加，保持纯文本干净
        m_caretShown = false;
    }
    c.insertText(delta);
    c.insertText(kCaret);
    m_caretShown = true;
    if (follow) {
        m_body->setTextCursor(c);
        m_body->ensureCursorVisible(); // 贴底才把内部滚动带到尾部
    }
}

void MessageBubble::finalizeMarkdown(const QString &fullText, bool endedInFence)
{
    Q_UNUSED(endedInFence); // 未闭合围栏块 Qt markdown 导入按 GFM 原文呈现
    if (!m_body)
        return;
    m_streaming = false;
    m_caretShown = false;
    // 定稿唯一重渲染点：一次 setMarkdown（含代码高亮），流式期间零重渲染
    m_body->setMarkdown(fullText);
    m_body->setVisible(!fullText.isEmpty()); // 空回复（如错误回合）不露空壳
    updateToolbar();
}

void MessageBubble::appendReasoningDelta(const QString &delta)
{
    if (delta.isEmpty())
        return;
    if (!m_reasonSection->isVisible())
        m_reasonSection->setVisible(true); // 首增量：折叠区出现（默认仍折叠）
    m_reasoningText += delta;
    m_reasonBody->setPlainText(m_reasoningText);
    updateReasoningHeader();
}

void MessageBubble::setReasoningFinal(const QString &full)
{
    // 定稿以模块回传的完整 reasoning 为准（流式已展示过的话内容一致）
    m_reasonFinalized = true;
    if (full.isEmpty()) {
        m_reasonSection->setVisible(false);
        return;
    }
    if (!m_reasonSection->isVisible())
        m_reasonSection->setVisible(true);
    m_reasoningText = full;
    m_reasonBody->setPlainText(full);
    updateReasoningHeader();
}

void MessageBubble::updateReasoningHeader()
{
    if (!m_reasonHead)
        return;
    const int n = m_reasoningText.size();
    m_reasonHead->setText(m_reasonFinalized
                              ? QStringLiteral("思考过程（%1 字）").arg(n)
                              : QStringLiteral("思考中…（%1 字）").arg(n));
}

// ---- 状态 ----

void MessageBubble::setRegenerateVisible(bool on)
{
    if (m_btnRegen)
        m_btnRegen->setVisible(on);
}

void MessageBubble::setMaxContentWidth(int paneWidth)
{
    if (m_role == User) {
        setMaximumWidth(int(paneWidth * 0.82)); // 贴纸气泡宽度上限
    } else {
        setMaximumWidth(QWIDGETSIZE_MAX);       // assistant/系统行占满行宽
    }
}

void MessageBubble::applyTheme(bool dark)
{
    // T-070 dsh 形态：配色取 GoodyTheme（气泡→文档流——assistant 无卡片框，
    // user 右对齐淡蓝圆角泡）。P-023 双通道纪律沿用：内联样式 + 调色板钉扎
    const bool darkTheme = dark;
    switch (m_role) {
    case User: {
        // dsh UserMessageBubble：淡蓝实底（浅色 deepseek-50/深色 neutral-850）
        // + 20px 圆角 + 深字
        setStyleSheet(QStringLiteral(
            "QFrame#goodyBubbleUser { background: %1; border-radius: 20px; }")
            .arg(goody_theme::userBubbleBg().name()));
        m_userText->setStyleSheet(QStringLiteral(
            "QLabel#goodyUserText { color: %1; font-size: 14px;"
            "  background: transparent; }").arg(goody_theme::textPrimary().name()));
        {
            QPalette pal = m_userText->palette();
            const QBrush textBrush{goody_theme::textPrimary()};
            for (const QPalette::ColorGroup group :
                 {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
                pal.setBrush(group, QPalette::WindowText, textBrush);
                pal.setBrush(group, QPalette::Text, textBrush);
            }
            m_userText->setPalette(pal);
        }
        break;
    }
    case Assistant: {
        // dsh AssistantMarkdown：全宽文档流——无卡片框无底色，正文 14px
        setStyleSheet(QStringLiteral(
            "QFrame#goodyBubbleAssistant { background: transparent; border: none; }"));
        m_body->setStyleSheet(QStringLiteral(
            "QTextBrowser#goodyBubbleText { color: %1; background: transparent;"
            "  font-size: 14px; }").arg(goody_theme::textPrimary().name()));
        m_reasonBody->setStyleSheet(QStringLiteral(
            "QTextBrowser#goodyReasoningBody { color: %1; background: %2;"
            "  border-radius: 8px; padding: 4px; font-size: 13px; }")
            .arg(goody_theme::textSecondary().name(),
                 darkTheme ? QColor(38, 38, 40).name() : QColor(0xF2, 0xF5, 0xFB).name()));
        m_reasonHead->setStyleSheet(QStringLiteral(
            "QToolButton#goodyReasoningHead { color: %1; background: transparent;"
            "  border: none; font-size: 13px; padding: 2px 4px;"
            "  text-align: left; }").arg(goody_theme::textSecondary().name()));
        // 正文与思考区调色板钉扎（Base/Text 三组全钉）；正文底透明化
        {
            QPalette pal = m_body->palette();
            const QBrush textBrush{goody_theme::textPrimary()};
            const QBrush baseBrush{QColor(0, 0, 0, 0)}; // 文档流：无底
            for (const QPalette::ColorGroup group :
                 {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
                pal.setBrush(group, QPalette::Text, textBrush);
                pal.setBrush(group, QPalette::Base, baseBrush);
            }
            m_body->setPalette(pal);

            QPalette rpal = m_reasonBody->palette();
            const QBrush rtext{goody_theme::textSecondary()};
            const QBrush rbase{darkTheme ? QColor(38, 38, 40) : QColor(0xF2, 0xF5, 0xFB)};
            for (const QPalette::ColorGroup group :
                 {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
                rpal.setBrush(group, QPalette::Text, rtext);
                rpal.setBrush(group, QPalette::Base, rbase);
            }
            m_reasonBody->setPalette(rpal);
        }
        break;
    }
    case System:
        setStyleSheet(errorLine() ? QStringLiteral(
            "QFrame#goodyBubbleSystemError { background: %1; border-radius: 8px; }")
            .arg(goody_theme::dark() ? QColor(0x2A, 0x14, 0x14).name()
                                     : QColor(0xFD, 0xF0, 0xEF).name())
            : QStringLiteral("QFrame#goodyBubbleSystem { background: transparent; }"));
        m_systemText->setStyleSheet(QStringLiteral(
            "QLabel#goodySystemText { color: %1; font-size: 12px;"
            "  background: transparent; }")
            .arg(errorLine() ? goody_theme::error().name()
                             : goody_theme::textTertiary().name()));
        {
            QPalette pal = m_systemText->palette();
            const QBrush textBrush{errorLine() ? goody_theme::error()
                                               : goody_theme::textTertiary()};
            for (const QPalette::ColorGroup group :
                 {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
                pal.setBrush(group, QPalette::WindowText, textBrush);
            m_systemText->setPalette(pal);
        }
        break;
    }

    // 工具条（user/assistant 共用）：悬浮浮层保留 dsh 视觉=卡片底+描边
    if (m_toolbar) {
        m_toolbar->setStyleSheet(QStringLiteral(
            "QFrame#goodyBubbleToolbar { background: %1; border: 1px solid %2;"
            "  border-radius: 8px; }")
            .arg(goody_theme::bgCard().name(), goody_theme::stroke().name()));
        for (QToolButton *btn :
             findChildren<QToolButton *>(QStringLiteral("goodyBubbleActBtn"))) {
            btn->setStyleSheet(QStringLiteral(
                "QToolButton#goodyBubbleActBtn { color: %1; background: transparent;"
                "  border: none; padding: 1px 5px; font-size: 11px; }"
                "QToolButton#goodyBubbleActBtn:hover { background: %2; }")
                .arg(goody_theme::textPrimary().name(),
                     goody_theme::bgHover().name()));
            QPalette pal = btn->palette();
            const QBrush textBrush{goody_theme::textPrimary()};
            for (const QPalette::ColorGroup group :
                 {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
                pal.setBrush(group, QPalette::ButtonText, textBrush);
                pal.setBrush(group, QPalette::Text, textBrush);
            }
            btn->setPalette(pal);
        }
        positionToolbar();
    }
    if (m_codeCopyBtn) {
        m_codeCopyBtn->setStyleSheet(QStringLiteral(
            "QToolButton#goodyCodeCopyBtn { color: %1; background: %2;"
            "  border: 1px solid %3; border-radius: 8px; padding: 1px 7px;"
            "  font-size: 11px; }")
            .arg(goody_theme::textPrimary().name(), goody_theme::bgCard().name(),
                 goody_theme::stroke().name()));
        QPalette pal = m_codeCopyBtn->palette();
        const QBrush textBrush{goody_theme::textPrimary()};
        for (const QPalette::ColorGroup group :
             {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
            pal.setBrush(group, QPalette::ButtonText, textBrush);
        m_codeCopyBtn->setPalette(pal);
    }

    // 语法高亮配色跟随主题
    if (m_highlighter)
        m_highlighter->setSyntaxColors(darkTheme ? CodeSyntaxColors::dark()
                                                 : CodeSyntaxColors::light());
}

bool MessageBubble::errorLine() const
{
    return m_role == System && objectName() == QLatin1String("goodyBubbleSystemError");
}

// ---- hover 工具条 ----

void MessageBubble::updateToolbar()
{
    // System 角色无工具条（m_toolbar 仅 buildUser/buildAssistant 创建）：
    // 流式/定稿路径只在 assistant 气泡上调本函数，此处守卫兜底空指针
    //（T-062 修复: 原 setStreaming 公开入口已删，防御对齐 positionToolbar）
    if (!m_toolbar)
        return;
    // 流式气泡不露工具条（重新生成/删除在回合中途不可用）
    m_toolbar->setVisible(m_hoverCount > 0 && !m_streaming);
    positionToolbar();
}

// 工具条 overlay 定位：气泡右上角内缩 6px，置顶防被正文盖住
void MessageBubble::positionToolbar()
{
    if (!m_toolbar)
        return;
    const int x = width() - m_toolbar->sizeHint().width() - 6;
    m_toolbar->move(x, 4);
    m_toolbar->raise();
}

void MessageBubble::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    positionToolbar();
}

bool MessageBubble::eventFilter(QObject *watched, QEvent *event)
{
    // Enter/Leave 计数（自身+全部后代）：移到子控件上工具条不闪
    if (event->type() == QEvent::Enter) {
        ++m_hoverCount;
        updateToolbar();
    } else if (event->type() == QEvent::Leave) {
        m_hoverCount = qMax(0, m_hoverCount - 1);
        updateToolbar();
    }
    // 正文 viewport 的 MouseMove：代码块行 hover 时浮出复制钮
    if (m_body && watched == m_body->viewport()) {
        if (event->type() == QEvent::MouseMove)
            handleViewportMove(static_cast<QMouseEvent *>(event)->pos());
        else if (event->type() == QEvent::Leave && m_codeCopyBtn)
            m_codeCopyBtn->hide();
    }
    return QFrame::eventFilter(watched, event);
}

QTextBlock MessageBubble::codeBlockAtViewport(const QPoint &pos) const
{
    if (!m_body)
        return QTextBlock();
    const QTextCursor cur = m_body->cursorForPosition(pos);
    QTextBlock b = cur.block();
    if (!b.isValid())
        return QTextBlock();
    // 围栏行本身（``` 开/闭栏）不放按钮：只有代码内容行触发
    if (!isCodeFenceBlock(b))
        return QTextBlock();
    if (b.text().trimmed().startsWith(QStringLiteral("```")))
        return QTextBlock();
    return b;
}

void MessageBubble::handleViewportMove(const QPoint &pos)
{
    const QTextBlock block = codeBlockAtViewport(pos);
    if (!block.isValid() || !m_codeCopyBtn) {
        m_codeCopyBtn->hide();
        return;
    }
    // 按钮浮在被 hover 的代码行右上角（viewport 坐标换算）
    const QRectF geo = m_body->document()->documentLayout()->blockBoundingRect(block);
    const int vx = int(geo.right()) - m_codeCopyBtn->sizeHint().width() + 14;
    const int vy = int(geo.top()) - m_body->verticalScrollBar()->value();
    m_codeCopyBtn->move(qBound(0, vx, m_body->viewport()->width()
                                     - m_codeCopyBtn->sizeHint().width()),
                        qMax(0, vy));
    m_codeCopyBtn->show();
    m_codeCopyBtn->raise();
}

void MessageBubble::copyCodeBlockUnder(const QTextBlock &block)
{
    // 收集连续代码内容行（isCodeFenceBlock 唯一口径；``` 围栏行已不落块，
    // 探针实测见 CodeHighlighter.h 头注），一键复制整块
    QTextBlock first = block;
    while (first.isValid()) {
        if (!isCodeFenceBlock(first))
            break;
        const QTextBlock prev = first.previous();
        if (!prev.isValid() || !isCodeFenceBlock(prev))
            break;
        first = prev;
    }
    QStringList lines;
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        if (!isCodeFenceBlock(b))
            break; // 出围栏即止
        if (b.text().trimmed().startsWith(QStringLiteral("```")))
            continue; // 保险：围栏行不进剪贴板
        lines << b.text();
    }
    QGuiApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    if (m_codeCopyBtn) {
        m_codeCopyBtn->setText(QStringLiteral("✓ 已复制"));
        QTimer::singleShot(kCopiedResetMs, this, [this] {
            if (m_codeCopyBtn)
                m_codeCopyBtn->setText(QStringLiteral("复制"));
        });
    }
}

} // namespace poptools
