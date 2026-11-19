#include "GoodyWindow.h"

#include <QClipboard>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLinearGradient>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizeGrip>
#include <QStyleHints>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/settings/Settings.h"
#include "core/assistant/ChatSession.h"
#include "core/assistant/ToolText.h"
#include "ui/ThemeColors.h" // systemSchemeDark
#include "ui/goody/GoodyTheme.h"
#include "ui/goody/ProcessRow.h"
#include "MessageBubble.h"

namespace poptools {

namespace {
constexpr int kCardRadius = 12; // = goody 贴桌面玻璃家族 signature 圆角
constexpr int kSidebarWidth = 280;   // T-070 dsh 侧栏默认宽
constexpr int kDefaultWidth = 760;   // T-050-P2 定案：居中卡片聊天窗默认尺寸
constexpr int kDefaultHeight = 600;
constexpr int kBottomSlackPx = 40;   // 贴底判定余量（自动滚动跟随门槛）
constexpr int kReasonThrottleMs = 150; // T-050-FIX2：reasoning 流式 flush 节流线（同正文 150ms）
constexpr int kInputMinH = 44;       // 输入框最小高（单行）
constexpr int kInputMaxH = goody_theme::kComposerMaxHeight; // 输入卡自适应上限

QString formatSessionTime(qint64 ms)
{
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ms);
    if (dt.date() == QDate::currentDate())
        return dt.toString(QStringLiteral("HH:mm"));
    return dt.toString(QStringLiteral("M-dd HH:mm"));
}
} // namespace

GoodyWindow::GoodyWindow(QWidget *parent)
    : QWidget(parent)
{
    // 窗口纪律（TranslateCard 先例）：无边框工具窗；聊天窗需
    // 正常激活抢焦点，不用 WA_ShowWithoutActivating
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setObjectName("goodyWindow");
    setWindowTitle(QStringLiteral("Goody 管家"));
    resize(kDefaultWidth, kDefaultHeight);
    setMinimumSize(560, 430);
    // T-050-P2 几何记忆（assistant/windowGeometry 新键）；无记忆 → 居中
    // 弹出（需求单定案：居中替代旧定位）
    if (!restoreGeometry(Settings::instance().assistantWindowGeometry())) {
        const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
        move(avail.center() - rect().center());
    }

    auto *rootLay = new QVBoxLayout(this);
    rootLay->setContentsMargins(1, 1, 1, 1);
    rootLay->setSpacing(0);

    auto *card = new QWidget(this);
    card->setObjectName("goodyCard");
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(12, 8, 12, 10);
    lay->setSpacing(8);
    rootLay->addWidget(card);

    // ---- 标题栏（侧栏开关 + 拖拽区 + 操作钮） ----
    auto *header = new QWidget(card);
    header->setObjectName("goodyHeader");
    header->setFixedHeight(34);
    header->installEventFilter(this); // 拖拽在 eventFilter 里做
    auto *headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(4, 0, 0, 0);
    headerLay->setSpacing(4);
    m_sidebarToggle = new QToolButton(header);
    m_sidebarToggle->setObjectName("goodySidebarToggle");
    m_sidebarToggle->setText(QStringLiteral("☰"));
    m_sidebarToggle->setToolTip(QStringLiteral("显示/隐藏会话侧栏"));
    m_sidebarToggle->setFixedSize(26, 24);
    headerLay->addWidget(m_sidebarToggle);
    auto *title = new QLabel(QStringLiteral("Goody 管家"), header);
    title->setObjectName("goodyTitle");
    headerLay->addWidget(title);
    headerLay->addStretch();
    m_clearBtn = new QPushButton(QStringLiteral("清空"), header);
    m_clearBtn->setObjectName("goodyHeaderBtn");
    m_clearBtn->setToolTip(QStringLiteral("清空当前会话（记忆与日志不受影响）"));
    headerLay->addWidget(m_clearBtn);
    auto *closeBtn = new QPushButton(QStringLiteral("✕"), header);
    closeBtn->setObjectName("goodyHeaderBtn");
    closeBtn->setFixedSize(26, 24);
    headerLay->addWidget(closeBtn);
    lay->addWidget(header);

    // ---- 主体：左侧会话侧栏 + 右侧对话区 ----
    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(8);
    buildSidebar();
    body->addWidget(m_sidebar);
    buildChatPane();
    body->addWidget(m_scroll, 1);
    lay->addLayout(body, 1);

    // ---- 权限确认卡片（默认隐藏；工具确认链路样式进新卡片设计） ----
    m_permBar = new QWidget(card);
    m_permBar->setObjectName("goodyPermBar");
    auto *permLay = new QVBoxLayout(m_permBar);
    permLay->setContentsMargins(10, 8, 10, 8);
    permLay->setSpacing(6);
    m_permText = new QLabel(m_permBar);
    m_permText->setObjectName("goodyPermText");
    m_permText->setWordWrap(true);
    permLay->addWidget(m_permText);
    auto *permBtnRow = new QHBoxLayout;
    permBtnRow->addStretch();
    m_permDeny = new QPushButton(QStringLiteral("拒绝"), m_permBar);
    m_permDeny->setObjectName("goodyPermDeny");
    m_permAlways = new QPushButton(QStringLiteral("总是允许此类"), m_permBar);
    m_permAlways->setObjectName("goodyPermBtn");
    m_permAllow = new QPushButton(QStringLiteral("本次允许"), m_permBar);
    m_permAllow->setObjectName("goodyPermAllow");
    permBtnRow->addWidget(m_permDeny);
    permBtnRow->addWidget(m_permAlways);
    permBtnRow->addWidget(m_permAllow);
    permLay->addLayout(permBtnRow);
    m_permBar->hide();
    lay->addWidget(m_permBar);

    // ---- 输入区（T-070 dsh composer：大圆角卡 + 自适应输入 + 同位发送钮） ----
    m_composerCard = new QFrame(card);
    m_composerCard->setObjectName("goodyComposer");
    auto *composerLay = new QHBoxLayout(m_composerCard);
    composerLay->setContentsMargins(12, 10, 10, 10);
    composerLay->setSpacing(8);
    m_input = new QPlainTextEdit(m_composerCard);
    m_input->setObjectName("goodyInput");
    m_input->setPlaceholderText(QStringLiteral("和 Goody 说点什么…（Enter 发送 / Shift+Enter 换行）"));
    m_input->setMinimumHeight(kInputMinH);
    m_input->setMaximumHeight(kInputMaxH);
    m_input->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_input->installEventFilter(this); // Enter 发送
    connect(m_input, &QPlainTextEdit::textChanged, this,
            [this] { adjustInputHeight(); });
    composerLay->addWidget(m_input, 1);
    // 发送/停止同位圆钮（dsh InputBar 状态机）：空闲 ↑ / busy ■（点击即停）
    m_sendBtn = new QPushButton(QStringLiteral("↑"), m_composerCard);
    m_sendBtn->setObjectName("goodySendBtn");
    m_sendBtn->setFixedSize(36, 36);
    m_sendBtn->setToolTip(QStringLiteral("发送（Enter）；Goody 工作时点击可停止"));
    composerLay->addWidget(m_sendBtn, 0, Qt::AlignBottom);
    lay->addWidget(m_composerCard);

    // 右下角缩放柄（无边框窗口的手动缩放；窗口可自由缩放）
    auto *grip = new QSizeGrip(card);
    grip->setFixedSize(14, 14);
    grip->setObjectName("goodyGrip");
    lay->addWidget(grip, 0, Qt::AlignBottom | Qt::AlignRight);

    // ---- 交互 ----
    connect(m_sendBtn, &QPushButton::clicked, this, [this] {
        if (m_busy) {
            emit stopRequested();
            return;
        }
        // T-050-FIX2：工具执行期吞掉发送且不清输入框（草稿保留）——原实现
        // 先清空草稿再被模块侧守卫拒绝，用户输入无感丢失
        if (m_toolPhase)
            return;
        const QString text = m_input->toPlainText().trimmed();
        if (text.isEmpty())
            return;
        m_input->clear();
        scrollToBottomNow(true); // 用户主动发送：强制贴底跟随
        emit sendRequested(text);
    });
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::hide);
    connect(m_sidebarToggle, &QToolButton::clicked, this, [this] {
        m_sidebar->setVisible(!m_sidebar->isVisible());
    });
    // 「清空」：窗口先收掉气泡（welcome 回补），域清空由模块守卫执行
    //（busy/工具期禁用见 updateClearBtnState，B-2/C-N2 红线）
    connect(m_clearBtn, &QPushButton::clicked, this, [this] {
        clearBubbles();
        appendWelcome();
        emit sessionCleared();
    });

    connect(m_permAllow, &QPushButton::clicked, this, [this] {
        if (m_permCallback)
            m_permCallback(true, false);
        m_permCallback = nullptr;
        m_permBar->hide();
    });
    connect(m_permAlways, &QPushButton::clicked, this, [this] {
        if (m_permCallback)
            m_permCallback(true, true);
        m_permCallback = nullptr;
        m_permBar->hide();
    });
    connect(m_permDeny, &QPushButton::clicked, this, [this] {
        if (m_permCallback)
            m_permCallback(false, false);
        m_permCallback = nullptr;
        m_permBar->hide();
    });

    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            &GoodyWindow::applyTheme);
    applyTheme();
    adjustInputHeight(); // 空 1 行基线高度
    appendWelcome();
}

// 输入框自适应高度（改版 M2）：按文档实际像素高 + 内边距收紧，钳制在
// 44~120px；textChanged 驱动，无常驻定时器
void GoodyWindow::adjustInputHeight()
{
    const int docH = qRound(m_input->document()->size().height());
    const int margins = m_input->contentsMargins().top() + m_input->contentsMargins().bottom();
    const int h = qBound(kInputMinH, docH + margins + 12, kInputMaxH);
    if (h != m_input->height())
        m_input->setFixedHeight(h);
}

// 构建左侧会话侧栏（新对话钮 + 条目列表；条目点击→sessionSwitchRequested，
// 由模块守卫执行域切换）
void GoodyWindow::buildSidebar()
{
    m_sidebar = new QWidget(this);
    m_sidebar->setObjectName("goodySidebar");
    m_sidebar->setFixedWidth(kSidebarWidth);
    auto *sideLay = new QVBoxLayout(m_sidebar);
    sideLay->setContentsMargins(4, 0, 0, 0);
    sideLay->setSpacing(6);

    m_newChatBtn = new QPushButton(QStringLiteral("＋ 新对话"), m_sidebar);
    m_newChatBtn->setObjectName("goodyNewChatBtn");
    m_newChatBtn->setToolTip(QStringLiteral("开始一个新会话"));
    sideLay->addWidget(m_newChatBtn);
    connect(m_newChatBtn, &QPushButton::clicked, this,
            [this] { emit sessionCreateRequested(); });

    m_sessionList = new QListWidget(m_sidebar);
    m_sessionList->setObjectName("goodySessionList");
    m_sessionList->setFrameShape(QFrame::NoFrame);
    m_sessionList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_sessionList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sessionList->setSelectionMode(QAbstractItemView::SingleSelection);
    sideLay->addWidget(m_sessionList, 1);
    // 切换会话：条目点击 → 模块守卫执行域切换（busy/工具期拒绝）
    connect(m_sessionList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *cur, QListWidgetItem *) {
                if (!cur || !m_session)
                    return;
                const QString id = cur->data(Qt::UserRole).toString();
                if (!id.isEmpty() && id != m_session->activeSessionId())
                    emit sessionSwitchRequested(id);
            });
}

// 构建右侧对话区（QScrollArea + 贴顶 stretch 的气泡流；valueChanged→贴底
// 判定 m_atBottom）
void GoodyWindow::buildChatPane()
{
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("goodyScroll");
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_msgList = new QWidget;
    m_msgList->setObjectName("goodyMsgList");
    m_msgLay = new QVBoxLayout(m_msgList);
    m_msgLay->setContentsMargins(6, 4, 6, 4);
    m_msgLay->setSpacing(8);
    m_msgLay->addStretch(); // 消息贴顶（列表不足一屏时），气泡插在 stretch 之前
    m_scroll->setWidget(m_msgList);
    // 自动滚动只在本就贴底时跟随：滚动位置离开底部即停止跟随（用户上翻
    // 不抢滚动条），回到底部恢复
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
        const QScrollBar *sb = m_scroll->verticalScrollBar();
        m_atBottom = v >= sb->maximum() - kBottomSlackPx;
    });
}

// ---- 会话注入与联动 ----

void GoodyWindow::setSession(ChatSession *session)
{
    if (m_session) {
        disconnect(m_session, &ChatSession::changed, this,
                   &GoodyWindow::onSessionDataChanged);
    }
    m_session = session;
    if (m_session) {
        connect(m_session, &ChatSession::changed, this,
                &GoodyWindow::onSessionDataChanged);
        reloadFromSession();
        refreshSessionList();
    }
}

// ChatSession::changed 联动：active 会话变化 → 气泡列表整体重建（懒构建
// 一次性）；仅消息/标题变化 → 侧栏刷新（流式期间推迟到定稿后）
void GoodyWindow::onSessionDataChanged()
{
    if (!m_session)
        return;
    if (m_session->activeSessionId() != m_builtSessionId) {
        reloadFromSession();
        refreshSessionList();
    } else {
        refreshSessionList();
    }
}

// 单条侧栏行装配（T-062 抽取）：标题/元信息/删除钮 + P-023 双通道配色 +
// 删除接线（T-070：配色切 GoodyTheme，行高对齐 dsh 32px 密度）
QWidget *GoodyWindow::makeSessionRow(const ChatSession::SessionSummary &s,
                                     bool dark)
{
    auto *row = new QWidget;
    row->setObjectName("goodySessionRow");
    auto *rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(8, 4, 4, 4);
    rowLay->setSpacing(4);
    auto *col = new QVBoxLayout;
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(1);
    auto *title = new QLabel(s.title.isEmpty() ? QStringLiteral("新对话") : s.title,
                             row);
    title->setObjectName("goodySessionTitle");
    auto *meta = new QLabel(QStringLiteral("%1 · %2 条")
                                .arg(formatSessionTime(s.updatedAtMs))
                                .arg(s.messageCount),
                            row);
    meta->setObjectName("goodySessionMeta");
    col->addWidget(title);
    col->addWidget(meta);
    rowLay->addLayout(col, 1);
    auto *del = new QToolButton(row);
    del->setObjectName("goodySessionDelBtn");
    del->setText(QStringLiteral("✕"));
    del->setToolTip(QStringLiteral("删除该会话"));
    del->setProperty("sessionId", s.id);
    rowLay->addWidget(del);
    // P-023：行内关键颜色双通道——ID 选择器内联样式 + 调色板钉扎
    Q_UNUSED(dark);
    row->setStyleSheet(QStringLiteral(
        "QWidget#goodySessionRow { background: transparent; }"
        "QLabel#goodySessionTitle { color: %1; font-size: 13px; }"
        "QLabel#goodySessionMeta { color: %2; font-size: 11px; }"
        "QToolButton#goodySessionDelBtn { color: %2; background: transparent;"
        "  border: none; font-size: 11px; }"
        "QToolButton#goodySessionDelBtn:hover { color: %3; }")
        .arg(goody_theme::textPrimary().name(), goody_theme::textTertiary().name(),
             goody_theme::error().name()));
    const auto pinLabelPalette = [&](QLabel *l, const QColor &color) {
        QPalette pal = l->palette();
        const QBrush b{color};
        for (const QPalette::ColorGroup group :
             {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
            pal.setBrush(group, QPalette::WindowText, b);
        l->setPalette(pal);
    };
    pinLabelPalette(title, goody_theme::textPrimary());
    pinLabelPalette(meta, goody_theme::textTertiary());
    const QString sid = s.id; // 结构化循环变量不可直接捕获，落为局部
    connect(del, &QToolButton::clicked, this, [this, sid] {
        emit sessionDeleteRequested(sid);
    });
    return row;
}

void GoodyWindow::refreshSessionList()
{
    if (!m_session)
        return;
    if (m_streaming) {
        m_sessionListDirty = true; // 定稿后统一刷新（流式热路径零 UI 开销）
        return;
    }
    m_sessionListDirty = false;
    m_sessionList->blockSignals(true); // 防重建触发 currentItemChanged
    m_sessionList->clear();
    const bool dark = systemSchemeDark();
    for (const ChatSession::SessionSummary &s : m_session->sessionSummaries()) {
        auto *item = new QListWidgetItem(m_sessionList);
        item->setData(Qt::UserRole, s.id);
        item->setSizeHint(QSize(0, 36));
        m_sessionList->setItemWidget(item, makeSessionRow(s, dark));
        if (s.id == m_session->activeSessionId())
            m_sessionList->setCurrentItem(item);
    }
    m_sessionList->blockSignals(false);
}

// 气泡列表整体重建（会话切换/删除/编辑重发/重新生成等一次性懒构建）
void GoodyWindow::reloadFromSession()
{
    if (!m_session || m_streaming)
        return; // 流式中不重建（模块侧守卫 + 双保险）
    clearBubbles();
    m_builtSessionId = m_session->activeSessionId();
    const QJsonArray msgs = m_session->messages();
    for (int i = 0; i < msgs.size(); ++i) {
        const QJsonObject o = msgs.at(i).toObject();
        const QString role = o.value(QStringLiteral("role")).toString();
        if (i == 0 && role == QLatin1String("system"))
            continue; // 人格提示词不展示
        if (role == QLatin1String("user")) {
            auto *b = new MessageBubble(MessageBubble::User, m_msgList);
            b->setUserText(o.value(QStringLiteral("content")).toString());
            addBubble(b);
        } else if (role == QLatin1String("assistant")) {
            auto *b = new MessageBubble(MessageBubble::Assistant, m_msgList);
            // 历史消息一次性构建：直接定稿渲染（含代码高亮），无流式路径
            b->finalizeMarkdown(o.value(QStringLiteral("content")).toString(), false);
            b->setReasoningFinal(o.value(QStringLiteral("reasoning")).toString());
            addBubble(b);
        } else if (role == QLatin1String("tool")) {
            const QJsonObject result = QJsonDocument::fromJson(
                o.value(QStringLiteral("content")).toString().toUtf8()).object();
            auto *b = new MessageBubble(MessageBubble::System, m_msgList);
            b->setSystemText(QStringLiteral("🔧 %1").arg(summarizeToolResult(result)),
                             false);
            addBubble(b);
        }
    }
    if (m_bubbles.isEmpty())
        appendWelcome();
    markRegenerateOnLast();
    scrollToBottomNow(true);
}

// ---- 气泡管理 ----

void GoodyWindow::addBubble(MessageBubble *bubble)
{
    m_bubbles.append(bubble);
    // T-070 dsh 形态：无头像无角色标签——user 右对齐、assistant/system 全宽
    if (bubble->role() == MessageBubble::User)
        m_msgLay->insertWidget(m_msgLay->count() - 1, bubble, 0, Qt::AlignRight);
    else
        m_msgLay->insertWidget(m_msgLay->count() - 1, bubble, 1);
    bubble->setMaxContentWidth(maxBubblePaneWidth());
    // T-050-FIX2：新增气泡在此局部套主题——追加路径不再全量重抛（窗口
    // QSS 重设 + 全气泡 re-theme + 侧栏全重建）；全量 applyTheme 仅保留在
    // 主题切换（colorSchemeChanged）与窗口初始化
    bubble->applyTheme(systemSchemeDark());
    connect(bubble, &MessageBubble::actionRequested, this,
            [this, bubble](int action) { onBubbleAction(bubble, action); });
}

// 气泡可用宽度：视口与消息列宽上限（T-070 dsh：内容列 clamp 至
// goody_theme::kMsgColumnMax，居中由 updateMaxWidths 的 margins 实现）
int GoodyWindow::maxBubblePaneWidth() const
{
    const int pane = m_scroll->viewport()->width();
    return pane > 0 ? qMin(pane, goody_theme::kMsgColumnMax) : pane;
}

void GoodyWindow::clearBubbles()
{
    for (MessageBubble *b : m_bubbles) {
        m_msgLay->removeWidget(b);
        b->deleteLater();
    }
    m_bubbles.clear();
    m_processRows.clear(); // 过程行随消息列表一并销毁（同为 m_msgList 子控件）
    m_streamBubble = nullptr;
}

void GoodyWindow::appendWelcome()
{
    auto *b = new MessageBubble(MessageBubble::System, m_msgList);
    b->setSystemText(QStringLiteral(
        "Goody 已就绪：问电脑相关的事、让我做记录，或者试试"
        "\"我今天干了什么\"。"), false);
    addBubble(b);
}

// user/assistant 气泡与会话内非 system/tool 消息按序一一对应；按"尾部
// 距离"换算下标——trim 只从头部按配对组淘汰，尾部距离恒稳定
int GoodyWindow::bubbleMessageIndex(MessageBubble *target) const
{
    int fromEnd = 0;
    bool found = false;
    for (int i = m_bubbles.size() - 1; i >= 0; --i) {
        MessageBubble *b = m_bubbles.at(i);
        if (b->role() == MessageBubble::System)
            continue;
        if (b == target) {
            found = true;
            break;
        }
        ++fromEnd;
    }
    if (!found || !m_session)
        return -1;
    const QJsonArray msgs = m_session->messages();
    int seen = -1;
    for (int i = msgs.size() - 1; i >= 0; --i) {
        const QString role =
            msgs.at(i).toObject().value(QStringLiteral("role")).toString();
        if (role == QLatin1String("system") || role == QLatin1String("tool"))
            continue;
        ++seen;
        if (seen == fromEnd)
            return i;
    }
    return -1;
}

void GoodyWindow::markRegenerateOnLast()
{
    for (MessageBubble *b : m_bubbles)
        if (b->role() == MessageBubble::Assistant)
            b->setRegenerateVisible(false);
    if (m_busy || m_toolPhase)
        return; // 回合进行中不提供"重新生成"（模块侧同守卫）
    // 只有最后一条"非系统气泡是 assistant 且确为会话最后一条消息"时才
    // 提供"重新生成"（其后已有用户新发言时重生成会误删新对话）
    for (int i = m_bubbles.size() - 1; i >= 0; --i) {
        MessageBubble *b = m_bubbles.at(i);
        if (b->role() == MessageBubble::System)
            continue;
        if (b->role() == MessageBubble::Assistant && m_session) {
            const int idx = bubbleMessageIndex(b);
            b->setRegenerateVisible(idx >= 0
                                    && idx == m_session->messages().size() - 1);
        }
        break;
    }
}

// ---- 消息操作四件套 ----

void GoodyWindow::onBubbleAction(MessageBubble *bubble, int action)
{
    switch (static_cast<BubbleAction>(action)) {
    case BubbleCopy: {
        const int idx = bubbleMessageIndex(bubble);
        QString text;
        if (idx >= 0 && m_session)
            text = m_session->messages().at(idx).toObject()
                       .value(QStringLiteral("content")).toString();
        if (!text.isEmpty())
            QGuiApplication::clipboard()->setText(text);
        break;
    }
    case BubbleEditResend: {
        const int idx = bubbleMessageIndex(bubble);
        if (idx >= 0)
            emit editResendRequested(idx);
        break;
    }
    case BubbleDelete: {
        const int idx = bubbleMessageIndex(bubble);
        if (idx >= 0)
            emit messageDeleteRequested(idx);
        break;
    }
    case BubbleRegenerate:
        emit regenerateRequested();
        break;
    }
}

// ---- 回合链路（GoodyModule 驱动） ----

void GoodyWindow::appendUserBubble(const QString &text)
{
    auto *b = new MessageBubble(MessageBubble::User, m_msgList);
    b->setUserText(text);
    addBubble(b);
    scrollToBottomNow(true);
}

void GoodyWindow::beginAssistantBubble()
{
    m_renderer.begin();
    m_streamClock.restart();
    m_reasonClock.restart(); // T-050-FIX2：reasoning 节流线时钟同步起表
    m_streaming = true;
    m_streamReasoning.clear();
    m_reasonPending.clear();
    markRegenerateOnLast(); // 上一条 assistant 的"重新生成"随新回合熄灭
    m_streamBubble = new MessageBubble(MessageBubble::Assistant, m_msgList);
    m_streamBubble->beginStream();
    addBubble(m_streamBubble);
    scrollIfAtBottom();
}

void GoodyWindow::appendAssistantDelta(const QString &delta)
{
    if (!m_streaming || !m_streamBubble || delta.isEmpty())
        return;
    const MarkdownRenderStep step = m_renderer.feed(delta, m_streamClock.elapsed());
    if (step.finalize) { // 理论不会从 feed 产生，防御性兜底（feed 的 finalize 恒 false，唯一来源是 finish()）
        m_streamBubble->finalizeMarkdown(m_renderer.streamText(), step.endedInFence);
        return;
    }
    if (step.present) {
        m_streamBubble->appendStreamText(step.newText);
        scrollIfAtBottom(); // 贴底才跟随
    }
    // 未过节流线：仅累积（renderer 内部），零 UI 动作——呈现频率 ≤1/150ms
}

void GoodyWindow::appendReasoningDelta(const QString &delta)
{
    if (!m_streaming || !m_streamBubble || delta.isEmpty())
        return;
    m_streamReasoning += delta;
    // T-050-FIX2：reasoning 第二道 150ms 节流线（m_streamClock 同款注入时
    // 钟模式）——线上才把累积增量 flush 给气泡，气泡侧 setPlainText 全量
    // 重建的频率被钳到 ~7/s；标题行"思考中…（N 字）"随同一线刷新。定稿
    // 照旧由 finishAssistantBubble 的 setReasoningFinal 收口（含未 flush 余量）
    m_reasonPending += delta;
    if (m_reasonClock.elapsed() >= kReasonThrottleMs) {
        m_streamBubble->appendReasoningDelta(m_reasonPending);
        m_reasonPending.clear();
        m_reasonClock.restart();
        scrollIfAtBottom();
    }
    // 未过节流线：仅累积，零 UI 动作
}

void GoodyWindow::finishAssistantBubble(const QString &fullText, const QString &reasoning)
{
    if (m_streamBubble) {
        const MarkdownRenderStep step = m_renderer.finish();
        // 定稿唯一重渲染点：一次 setMarkdown（代码高亮在此生效）
        m_streamBubble->finalizeMarkdown(fullText, step.endedInFence);
        m_streamBubble->setReasoningFinal(reasoning.isEmpty() ? m_streamReasoning
                                                              : reasoning);
        if (!fullText.isEmpty())
            scrollIfAtBottom();
    }
    m_streaming = false;
    m_streamBubble = nullptr;
    m_streamReasoning.clear();
    m_reasonPending.clear(); // T-050-FIX2：节流线余量随定稿丢弃（定稿文本已含全量）
    markRegenerateOnLast();
    if (m_sessionListDirty)
        refreshSessionList(); // 流式期间被推迟的侧栏刷新
}

void GoodyWindow::appendSystemLine(const QString &text)
{
    auto *b = new MessageBubble(MessageBubble::System, m_msgList);
    b->setSystemText(text, false);
    addBubble(b);
    scrollIfAtBottom();
}

void GoodyWindow::showErrorLine(const QString &text)
{
    auto *b = new MessageBubble(MessageBubble::System, m_msgList);
    b->setSystemText(text, true);
    addBubble(b);
    scrollToBottomNow(true); // 错误提示必须被看见
}

// ---- 权限确认卡片 ----

void GoodyWindow::promptPermission(const QString &tool, const QString &argsSummary,
                                   const std::function<void(bool, bool)> &onDecide)
{
    // T-062 修复: 第二次 promptPermission 原先无条件覆盖 m_permCallback，
    // 未决的第一份回调被静默丢弃——那个工具调用的回合链永久悬置（toolPhase
    // 不再收束）。当前 GoodyModule::processNextToolCall 严格单飞（一次只
    // 确认一个工具），本分支现网不可达，纯防御兜底：按拒绝语义收口旧回调
    if (m_permCallback) {
        m_permCallback(false, false);
        m_permCallback = nullptr;
    }
    m_permCallback = onDecide;
    m_permText->setText(QStringLiteral("Goody 请求执行 <b>%1</b><br>%2")
                            .arg(tool.toHtmlEscaped(), argsSummary.toHtmlEscaped()));
    m_permBar->show();
    scrollToBottomNow(true);
}

// ---- 状态守卫（B-2/C-N2） ----

void GoodyWindow::setBusy(bool on)
{
    // T-070：发送/停止同位钮形态切换（dsh InputBar 状态机）
    m_busy = on;
    m_sendBtn->setText(on ? QStringLiteral("■") : QStringLiteral("↑"));
    m_input->setEnabled(true); // busy 时仍可输入下一条，但 Enter 不发送
    // busy（LLM 请求在途）同样禁用「清空」（C-N2 第一道保险）：清空后迟到
    // 回包 unconditional append 进空会话，断裂序列会让下一轮请求 400。
    // 与工具期联合判断，避免两阶段交错时按钮态被单侧复位
    updateClearBtnState();
    updateSessionUiLocked();
}

// ---- T-070 工具过程行（dsh ToolRow 形态） ----

// 工具发起：插入过程行（运行中徽章），行随消息列表滚动
void GoodyWindow::toolRowStarted(const QString &callId, const QString &name,
                                 const QString &argsSummary)
{
    auto *row = new ProcessRow(name, argsSummary, m_msgList);
    m_msgLay->insertWidget(m_msgLay->count() - 1, row, 1);
    m_processRows.insert(callId, row);
    scrollIfAtBottom();
}

// 工具结束：徽章翻绿/红 + 结果进展开体（默认折叠，点行查看）
void GoodyWindow::toolRowFinished(const QString &callId, bool ok,
                                  const QString &resultText)
{
    if (ProcessRow *row = m_processRows.take(callId))
        row->finish(ok ? ProcessRow::State::Ok : ProcessRow::State::Error,
                    resultText);
}

void GoodyWindow::setToolPhase(bool on)
{
    // 工具执行期（含权限确认等待）禁用「清空」（B-2）：会话清空后剩余工具
    // 结果继续回填会留下孤儿 tool 序列，之后每轮请求被 API 整包拒绝
    m_toolPhase = on;
    // T-050-FIX2：进入工具期同步熄灭"重新生成"——finishAssistantBubble 内
    // 的 markRegenerateOnLast 先于本调用点亮了新气泡的按钮，且本处原先不
    // 回调熄灭；m_toolPhase=true 使 markRegenerateOnLast 只熄灭不再点亮
    if (on)
        markRegenerateOnLast();
    updateClearBtnState();
    updateSessionUiLocked();
}

void GoodyWindow::updateClearBtnState()
{
    const bool locked = m_busy || m_toolPhase;
    m_clearBtn->setEnabled(!locked);
    m_clearBtn->setToolTip(
        locked ? (m_toolPhase ? QStringLiteral("工具执行中，暂不可清空会话")
                              : QStringLiteral("回复进行中，暂不可清空会话"))
               : QStringLiteral("清空当前会话（记忆与日志不受影响）"));
}

void GoodyWindow::updateSessionUiLocked()
{
    // busy/工具期联合禁会话切换/新建/删除（与「清空」同一守卫语义：
    // 中途切会话会让在途回包写错会话，等价 C-N2 场景）
    const bool locked = m_busy || m_toolPhase;
    m_sessionList->setEnabled(!locked);
    m_newChatBtn->setEnabled(!locked);
}

void GoodyWindow::setDraftInput(const QString &text)
{
    m_input->setPlainText(text);
    m_input->moveCursor(QTextCursor::End);
    m_input->setFocus();
}

// ---- 滚动 ----

// 仅在本就贴底时跟随。T-062: 与 scrollToBottomNow(false) 逐行为一致（不改
// m_atBottom、同样排队贴底），原逐行实现合并为一行委托
void GoodyWindow::scrollIfAtBottom()
{
    scrollToBottomNow(false);
}

// force=true 先置贴底标记再排队贴底（用户主动发送/错误行等"必须看见"场景）；
// force=false 与 scrollIfAtBottom 同义。布局在事件循环里才刷新滚动范围：
// 排队执行贴底（非常驻定时器）
void GoodyWindow::scrollToBottomNow(bool force)
{
    if (force)
        m_atBottom = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            QScrollBar *sb = m_scroll->verticalScrollBar();
            if (m_atBottom)
                sb->setValue(sb->maximum());
        },
        Qt::QueuedConnection);
}

// ---- 窗口事件 ----

void GoodyWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect(), kCardRadius, kCardRadius);
    p.fillPath(path, goody_theme::bgBase()); // 胶囊底：整体一张圆角卡（dsh 基底色）
}

void GoodyWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    // T-050-P2：几何记忆新键（window self-manage）
    Settings::instance().setAssistantWindowGeometry(saveGeometry());
    emit windowHidden();
}

void GoodyWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateMaxWidths(); // 窗口缩放 → 气泡宽度上限与高度自适应跟随
}

void GoodyWindow::updateMaxWidths()
{
    // T-070 dsh：消息列 clamp 至 kMsgColumnMax 且居中（margins 动态收放）
    const int pane = m_scroll->viewport()->width();
    if (pane <= 0)
        return;
    const int content = qMin(pane, goody_theme::kMsgColumnMax);
    const int side = (pane - content) / 2;
    m_msgLay->setContentsMargins(side, m_msgLay->contentsMargins().top(), side,
                                 m_msgLay->contentsMargins().bottom());
    for (MessageBubble *b : m_bubbles)
        b->setMaxContentWidth(content);
}

bool GoodyWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 输入框 Enter 发送（Shift+Enter 换行）
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            if (!(ke->modifiers() & Qt::ShiftModifier)) {
                // T-050-FIX2：工具执行期与 busy 同语义——Enter 吞掉、不清
                // 输入框（草稿保留，等工具期结束再发）
                if (!m_busy && !m_toolPhase) {
                    const QString text = m_input->toPlainText().trimmed();
                    if (!text.isEmpty()) {
                        m_input->clear();
                        scrollToBottomNow(true);
                        emit sendRequested(text);
                    }
                }
                return true; // busy/工具期 Enter 吞掉（不打进输入框）
            }
        }
        return QWidget::eventFilter(watched, event);
    }
    // 标题栏拖拽（无边框窗口的移动）
    if (watched->objectName() == QLatin1String("goodyHeader")) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_dragAt = me->globalPosition().toPoint() - frameGeometry().topLeft();
                m_dragging = true;
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && m_dragging) {
            auto *me = static_cast<QMouseEvent *>(event);
            move(me->globalPosition().toPoint() - m_dragAt);
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease) {
            m_dragging = false;
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ---- 主题（P-022 ID 选择器 + P-023 双通道） ----

// T-062 拆分说明：applyTheme 原为约 130 行巨型函数（窗口 QSS/控件内联样式/
// 调色板钉扎/气泡重抛/侧栏重建七段），拆为 applyWindowQss + applyControlPalettes
// 两个纯搬移 helper，主干只留编排顺序（纯函数分解，调用顺序不变）

// 窗口级 QSS：只保留布局类声明（P-022 一律 ID 选择器；颜色取 Fluent token）
void GoodyWindow::applyWindowQss(bool dark)
{
    // R6-B3/P-023：窗口级样式表只保留布局类声明 + ID 选择器（P-022）；
    // 颜色取 GoodyTheme（T-070 dsh 形态）。关键颜色双通道钉扎在
    // applyControlPalettes
    const QColor bgBase = goody_theme::bgBase();
    const QColor stroke = goody_theme::stroke();
    const QColor strokeSoft = goody_theme::strokeSoft();
    const QColor textPrimary = goody_theme::textPrimary();
    const QColor textSecondary = goody_theme::textSecondary();
    const QColor textTertiary = goody_theme::textTertiary();
    const QColor accent = goody_theme::accent();
    const QColor accentHover = goody_theme::accentHover();
    const QColor onAccent = QColor(0xFF, 0xFF, 0xFF);
    const QColor bgCard = goody_theme::bgCard();
    const QColor bgHover = goody_theme::bgHover();
    const QColor inputBg = goody_theme::inputBg();
    const QColor sidebar = goody_theme::bgSidebar();
    const QColor error = goody_theme::error();
    const QColor warning = goody_theme::warning();
    setStyleSheet(QStringLiteral(
        "#goodyCard { background: %1; border: 1px solid %2; border-radius: 12px; }"
        "#goodyTitle { color: %3; font-weight: 600; font-size: %4px; }"
        "QPushButton#goodyHeaderBtn { background: transparent; border: none;"
        "  font-size: %5px; padding: 2px 8px; border-radius: 8px; color: %3; }"
        "QPushButton#goodyHeaderBtn:hover { background: %6; }"
        // 发送/停止同位圆钮（dsh InputBar 状态机：蓝↑ / 方块 stop）
        "QPushButton#goodySendBtn { background: %7; color: %8;"
        "  border: none; border-radius: 8px; font-weight: 600; font-size: %5px; }"
        "QPushButton#goodySendBtn:hover { background: %9; }"
        "QToolButton#goodySidebarToggle { color: %3; background: transparent;"
        "  border: none; font-size: 14px; border-radius: 8px; }"
        "QToolButton#goodySidebarToggle:hover { background: %6; }"
        "QWidget#goodySidebar { background: %10; border-right: 1px solid %2;"
        "  border-radius: 8px; }"
        "QPushButton#goodyNewChatBtn { background: %6; color: %3;"
        "  border: 1px solid %2; border-radius: 8px; padding: 6px;"
        "  font-size: %5px; font-weight: 600; }"
        "QPushButton#goodyNewChatBtn:hover { background: %7; color: %8;"
        "  border-color: %7; }"
        "QListWidget#goodySessionList { background: transparent; border: none;"
        "  font-size: %5px; outline: none; }"
        "QListWidget#goodySessionList::item { margin: 1px 2px; border-radius: 8px; }"
        "QListWidget#goodySessionList::item:selected { background: %6; }"
        "QListWidget#goodySessionList::item:hover { background: %6; }"
        "QWidget#goodyScroll, QWidget#goodyMsgList { background: transparent; }"
        // composer 大圆角卡（dsh InputBar：radius-panel + 内嵌底色）
        "QFrame#goodyComposer { background: %11; border: 1px solid %2;"
        "  border-radius: %12px; }"
        "QPlainTextEdit#goodyInput { background: transparent; border: none; }"
        // 权限确认卡（dsh ApprovalCard：接管输入位，warning 描边大卡）
        "QWidget#goodyPermBar { background: %11; border: 1px solid %13;"
        "  border-radius: %12px; }"
        "QPushButton#goodyPermBtn, QPushButton#goodyPermDeny { background: transparent;"
        "  border: 1px solid %2; border-radius: 8px; padding: 5px 12px; color: %3; }"
        "QPushButton#goodyPermDeny:hover { border-color: %13; color: %13; }"
        "QPushButton#goodyPermAllow { background: %7; border: 1px solid %7;"
        "  border-radius: 8px; padding: 5px 12px; color: %8; font-weight: 600; }"
        "QPushButton#goodyPermAllow:hover { background: %9; }"
        "QWidget#goodyGrip { background: transparent; }"
        // 过程行（T-070 ProcessRow）
        "QWidget#goodyProcessRow { background: transparent; }"
        "QLabel#goodyProcessDot { border: none; }"
        "QToolButton#goodyProcessHead { background: transparent; border: none;"
        "  font-size: %14px; text-align: left; padding: 2px 4px; color: %3; }"
        "QTextBrowser#goodyProcessResult { color: %15; background: %6;"
        "  border: 1px solid %2; border-radius: 8px; font-size: %5px; }")
        .arg(bgBase.name(), stroke.name(), textPrimary.name(),
             QString::number(goody_theme::kFontTitle),
             QString::number(goody_theme::kFontProcess),
             bgHover.name(), accent.name(), onAccent.name(), accentHover.name(),
             sidebar.name(), inputBg.name(),
             QString::number(goody_theme::kRadiusPanel),
             warning.name(),
             QString::number(goody_theme::kFontProcess),
             textSecondary.name()));
}

// 通道一（控件内联样式）+ 通道二（Active/Inactive/Disabled 三组调色板钉扎）
void GoodyWindow::applyControlPalettes(bool dark)
{
    // P-023 通道二：三组调色板钉扎（T-070：取 GoodyTheme）
    const QColor textPrimary = goody_theme::textPrimary();
    const QColor inputBg = goody_theme::inputBg();
    const QColor accent = goody_theme::accent();

    // 占位字：正文色 47% 透明度（深浅底都可读且别于正文）
    QColor phColor{goody_theme::textPrimary()};
    phColor.setAlpha(120);
    const auto pinEditPalette = [&](QWidget *edit, bool placeholder) {
        QPalette pal = edit->palette();
        const QBrush textBrush{textPrimary};
        const QBrush editBgBrush{inputBg};
        for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive,
                                                 QPalette::Disabled}) {
            pal.setBrush(group, QPalette::Text, textBrush);
            pal.setBrush(group, QPalette::HighlightedText, textBrush);
            pal.setBrush(group, QPalette::Base, editBgBrush);
            pal.setBrush(group, QPalette::Window, editBgBrush);
            if (placeholder)
                pal.setBrush(group, QPalette::PlaceholderText, QBrush{phColor});
        }
        edit->setPalette(pal);
        edit->setAutoFillBackground(true);
    };
    pinEditPalette(m_input, true);

    const auto pinButtonPalette = [&](QAbstractButton *btn, const QBrush &brush) {
        QPalette pal = btn->palette();
        for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive,
                                                 QPalette::Disabled}) {
            pal.setBrush(group, QPalette::ButtonText, brush);
            pal.setBrush(group, QPalette::Text, brush);
        }
        btn->setPalette(pal);
    };
    const QBrush textBrush{textPrimary};
    for (QPushButton *btn : {m_permDeny, m_permAlways})
        pinButtonPalette(btn, textBrush);
    pinButtonPalette(m_permAllow, QBrush{goody_theme::onAccent()});
    pinButtonPalette(m_newChatBtn, textBrush);
    pinButtonPalette(m_sidebarToggle, textBrush);
    {
        QPalette pal = m_permText->palette();
        for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive,
                                                 QPalette::Disabled})
            pal.setBrush(group, QPalette::Text, textBrush);
        m_permText->setPalette(pal);
    }
    // 会话列表调色板钉扎（选中底/字取主题色，防 QSS 失效黑字）
    {
        QPalette pal = m_sessionList->palette();
        const QBrush selBrush{goody_theme::bgHover()};
        for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive,
                                                 QPalette::Disabled}) {
            pal.setBrush(group, QPalette::Text, textBrush);
            pal.setBrush(group, QPalette::HighlightedText, textBrush);
            pal.setBrush(group, QPalette::Highlight, selBrush);
            pal.setBrush(group, QPalette::Base, QColor(0, 0, 0, 0));
        }
        m_sessionList->setPalette(pal);
    }
    Q_UNUSED(dark);
}

void GoodyWindow::applyTheme()
{
    const bool dark = systemSchemeDark();
    applyWindowQss(dark);
    applyControlPalettes(dark);

    for (MessageBubble *b : m_bubbles)
        b->applyTheme(dark); // 气泡自带双通道（含代码高亮配色跟随）
    // 过程行徽章/配色重抛（T-070：重置态色随主题刷新——经 finish 原态重涂）
    for (ProcessRow *row : m_processRows)
        row->setSummary(row->property("goodySummary").toString());

    refreshSessionList(); // 侧栏行配色随主题重建（不回环：刷新不触发 applyTheme）
}

} // namespace poptools
