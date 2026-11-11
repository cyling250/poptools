#include "GoodyWindow.h"

#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSizeGrip>
#include <QStyleHints>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#include "ui/ThemeColors.h"

namespace poptools {

namespace {
constexpr int kCardRadius = 12;
constexpr int kRenderIntervalMs = 100; // 流式重绘节流
} // namespace

GoodyWindow::GoodyWindow(QWidget *parent)
    : QWidget(parent)
{
    // 窗口纪律（复用 BoxWindow/TranslateCard 全套）：无边框工具窗，
    // 不置顶（聊天窗需要正常激活抢焦点）；WA_ShowWithoutActivating 不用
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setObjectName("goodyWindow");
    setWindowTitle(QStringLiteral("Goody 管家"));
    resize(440, 600);
    setMinimumSize(360, 420);

    auto *rootLay = new QVBoxLayout(this);
    rootLay->setContentsMargins(1, 1, 1, 1);
    rootLay->setSpacing(0);

    auto *card = new QWidget(this);
    card->setObjectName("goodyCard");
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(12, 8, 12, 10);
    lay->setSpacing(8);
    rootLay->addWidget(card);

    // ---- 标题栏（拖拽区 + 操作钮） ----
    auto *header = new QWidget(card);
    header->setObjectName("goodyHeader");
    header->setFixedHeight(34);
    header->installEventFilter(this); // 拖拽在 eventFilter 里做
    auto *headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(4, 0, 0, 0);
    headerLay->setSpacing(4);
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

    // ---- 气泡流 ----
    m_transcript = new QTextBrowser(card);
    m_transcript->setObjectName("goodyTranscript");
    m_transcript->setFrameShape(QFrame::NoFrame);
    m_transcript->setOpenExternalLinks(true);
    lay->addWidget(m_transcript, 1);

    // ---- 权限确认横条（默认隐藏） ----
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

    // ---- 输入区 ----
    auto *inputRow = new QHBoxLayout;
    inputRow->setSpacing(6);
    m_input = new QPlainTextEdit(card);
    m_input->setObjectName("goodyInput");
    m_input->setPlaceholderText(QStringLiteral("和 Goody 说点什么…（Enter 发送）"));
    m_input->setFixedHeight(56);
    m_input->installEventFilter(this); // Enter 发送
    inputRow->addWidget(m_input, 1);
    m_sendBtn = new QPushButton(QStringLiteral("发送"), card);
    m_sendBtn->setObjectName("goodySendBtn");
    m_sendBtn->setFixedHeight(56);
    inputRow->addWidget(m_sendBtn);
    lay->addLayout(inputRow);

    // 右下角缩放柄（无边框窗口的手动缩放）
    auto *grip = new QSizeGrip(card);
    grip->setFixedSize(14, 14);
    grip->setObjectName("goodyGrip");
    lay->addWidget(grip, 0, Qt::AlignBottom | Qt::AlignRight);

    // ---- 流式渲染节流 ----
    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(kRenderIntervalMs);
    connect(m_renderTimer, &QTimer::timeout, this, [this] { renderTranscript(); });

    // ---- 交互 ----
    connect(m_sendBtn, &QPushButton::clicked, this, [this] {
        if (m_busy) {
            emit stopRequested();
            return;
        }
        const QString text = m_input->toPlainText().trimmed();
        if (text.isEmpty())
            return;
        m_input->clear();
        emit sendRequested(text);
    });
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::hide);
    connect(m_clearBtn, &QPushButton::clicked, this, [this] {
        m_bubbles.clear();
        renderTranscript();
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

    appendSystemLine(QStringLiteral("Goody 已就绪：问电脑相关的事、让我做记录，或者试试"
                                    "\"我今天干了什么\"。"));
}

bool GoodyWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 输入框 Enter 发送（Shift+Enter 换行）
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            if (!(ke->modifiers() & Qt::ShiftModifier)) {
                if (!m_busy) {
                    const QString text = m_input->toPlainText().trimmed();
                    if (!text.isEmpty()) {
                        m_input->clear();
                        emit sendRequested(text);
                    }
                }
                return true; // busy 时 Enter 吞掉（不打进输入框）
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

void GoodyWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect(), kCardRadius, kCardRadius);
    const ThemeColors c = uiThemeColors(systemSchemeDark());
    p.fillPath(path, QColor(c.cardBg)); // 胶囊底：整体一张圆角卡
}

void GoodyWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    emit windowHidden();
}

void GoodyWindow::applyTheme()
{
    const ThemeColors c = uiThemeColors(systemSchemeDark());
    // R6-B3: 关键颜色双通道（照 TranslateCard::applyTheme 模式，P-023 家族
    // 整改漏网项）——气泡流/输入框/权限条 chrome 此前只有窗口级样式表一条
    // 颜色通道，故障机（部分 Win10 渲染路径）上失效会黑字落深底不可读。
    // 现改为：控件内联样式（颜色取 ThemeColors）+ Active/Inactive/Disabled
    // 三组调色板钉扎双保险；窗口级样式表只保留布局类声明（背景/边框/
    // 圆角/内边距/悬停底色）。P-022 纪律：QSS 一律 ID 选择器
    setStyleSheet(QStringLiteral(
        "#goodyCard { background: %1; border: 1px solid %2; border-radius: 12px; }"
        "#goodyTitle { color: %3; font-weight: 600; font-size: 13px; }"
        "QPushButton#goodyHeaderBtn { background: transparent; border: none;"
        "  font-size: 12px; padding: 2px 8px; border-radius: 6px; }"
        "QPushButton#goodyHeaderBtn:hover { background: %4; }"
        "QPushButton#goodySendBtn { background: %5; color: #FFFFFF;"
        "  border: 1px solid %5; border-radius: 8px; font-weight: 600; }"
        "QPushButton#goodySendBtn:hover { background: %6; }"
        "QWidget#goodyPermBar { background: %4; border: 1px solid %2;"
        "  border-radius: 8px; }"
        "QPushButton#goodyPermBtn, QPushButton#goodyPermDeny { background: %7;"
        "  border: 1px solid %2; border-radius: 6px; padding: 3px 10px; }"
        "QPushButton#goodyPermDeny:hover { border-color: #E05555; }"
        "QPushButton#goodyPermAllow { background: %5; border: 1px solid %5;"
        "  border-radius: 6px; padding: 3px 10px; }"
        "QWidget#goodyGrip { background: transparent; }")
        .arg(c.cardBg, c.editBorder, c.title, c.ctrlHover, c.accent,
             c.accentHover, c.ctrlBg));

    // 通道一（内联样式）：气泡流/输入框整体颜色声明改写在控件自身
    m_transcript->setStyleSheet(QStringLiteral(
        "QTextBrowser#goodyTranscript { color: %1; background: %2;"
        "  border: 1px solid %3; border-radius: 8px; padding: 8px; font-size: 13px; }")
        .arg(c.body, c.editBg, c.editBorder));
    m_input->setStyleSheet(QStringLiteral(
        "QPlainTextEdit#goodyInput { color: %1; background: %2;"
        "  border: 1px solid %3; border-radius: 8px; padding: 6px; font-size: 13px; }"
        "QPlainTextEdit#goodyInput:focus { border-color: %4; }")
        .arg(c.body, c.editBg, c.editBorder, c.accent));
    m_permText->setStyleSheet(QStringLiteral(
        "QLabel#goodyPermText { color: %1; font-size: 12px; }").arg(c.body));
    for (QPushButton *btn : findChildren<QPushButton *>(QStringLiteral("goodyHeaderBtn")))
        btn->setStyleSheet(QStringLiteral(
            "QPushButton#goodyHeaderBtn { color: %1; }").arg(c.body));
    m_permDeny->setStyleSheet(QStringLiteral(
        "QPushButton#goodyPermDeny { color: %1; }"
        "QPushButton#goodyPermDeny:hover { color: #E05555; }").arg(c.body));
    m_permAlways->setStyleSheet(QStringLiteral(
        "QPushButton#goodyPermBtn { color: %1; }").arg(c.body));
    m_permAllow->setStyleSheet(QStringLiteral(
        "QPushButton#goodyPermAllow { color: #FFFFFF; }"));

    // 通道二（调色板钉扎）：三个组全部钉到主题色，QSS 任何路径失效都有兜底。
    // 文本区钉 Text/HighlightedText/Base/Window；m_input 另钉 PlaceholderText
    //（占位字此前依赖系统调色板，深色主题下有不可读风险）；按钮钉
    // ButtonText/Text；权限提示条 QLabel 钉 Text
    const QBrush textBrush{QColor(c.body)};
    const QBrush editBgBrush{QColor(c.editBg)};
    // 占位字：主题表无占位色，取正文色 47% 透明度（深浅底都可读且别于正文）
    QColor phColor{QColor(c.body)};
    phColor.setAlpha(120);
    const QBrush phBrush{phColor};
    auto pinEditPalette = [&](QWidget *edit, bool placeholder) {
        QPalette pal = edit->palette();
        for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive,
                                                 QPalette::Disabled}) {
            pal.setBrush(group, QPalette::Text, textBrush);
            pal.setBrush(group, QPalette::HighlightedText, textBrush);
            pal.setBrush(group, QPalette::Base, editBgBrush);
            pal.setBrush(group, QPalette::Window, editBgBrush);
            if (placeholder)
                pal.setBrush(group, QPalette::PlaceholderText, phBrush);
        }
        edit->setPalette(pal);
        edit->setAutoFillBackground(true);
    };
    pinEditPalette(m_transcript, false);
    pinEditPalette(m_input, true);

    auto pinButtonPalette = [&](QPushButton *btn, const QBrush &brush) {
        QPalette pal = btn->palette();
        for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive,
                                                 QPalette::Disabled}) {
            pal.setBrush(group, QPalette::ButtonText, brush);
            pal.setBrush(group, QPalette::Text, brush);
        }
        btn->setPalette(pal);
    };
    for (QPushButton *btn : findChildren<QPushButton *>(QStringLiteral("goodyHeaderBtn")))
        pinButtonPalette(btn, textBrush);
    for (QPushButton *btn : {m_permDeny, m_permAlways})
        pinButtonPalette(btn, textBrush);
    pinButtonPalette(m_permAllow, QBrush{Qt::white});
    {
        QPalette pal = m_permText->palette();
        for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive,
                                                 QPalette::Disabled})
            pal.setBrush(group, QPalette::Text, textBrush);
        m_permText->setPalette(pal);
    }

    renderTranscript(); // 主题切换后重绘（内容无颜色标记，配色全走样式/调色板）
}

// ---- 气泡流渲染 ----

static QString escapeHtml(const QString &s)
{
    QString r = s;
    r.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    r.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    r.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    return r;
}

void GoodyWindow::appendUserBubble(const QString &text)
{
    m_bubbles.append({QStringLiteral("user"), text, false});
    renderTranscript();
}

void GoodyWindow::beginAssistantBubble()
{
    m_streamBuf.clear();
    m_streaming = true;
    m_bubbles.append({QStringLiteral("assistant"), QString(), false});
}

void GoodyWindow::appendAssistantDelta(const QString &delta)
{
    m_streamBuf += delta;
    scheduleRender();
}

void GoodyWindow::finishAssistantBubble(const QString &fullText)
{
    m_streaming = false;
    m_streamBuf.clear();
    if (!m_bubbles.isEmpty() && m_bubbles.last().role == QLatin1String("assistant"))
        m_bubbles.last().text = fullText;
    renderTranscript();
}

void GoodyWindow::appendSystemLine(const QString &text)
{
    m_bubbles.append({QStringLiteral("system"), text, false});
    renderTranscript();
}

void GoodyWindow::showErrorLine(const QString &text)
{
    m_bubbles.append({QStringLiteral("system"), text, true});
    renderTranscript();
}

void GoodyWindow::scheduleRender()
{
    if (!m_renderTimer->isActive())
        m_renderTimer->start();
}

void GoodyWindow::renderTranscript()
{
    QString md;
    for (const Bubble &b : m_bubbles) {
        if (b.role == QLatin1String("user")) {
            md += QStringLiteral("**你**\n\n> %1\n\n").arg(escapeHtml(b.text));
        } else if (b.role == QLatin1String("assistant")) {
            const QString text = (m_streaming && &b == &m_bubbles.last()) ? m_streamBuf : b.text;
            md += QStringLiteral("**Goody**\n\n%1\n\n").arg(text);
            if (m_streaming && &b == &m_bubbles.last())
                md += QStringLiteral("▍\n\n"); // 打字光标
        } else {
            md += QStringLiteral("*%1*\n\n").arg(escapeHtml(b.text));
        }
    }
    // markdown 渲染（QTextBrowser 支持 GFM 子集：代码块/列表/粗体）
    m_transcript->setMarkdown(md);
    m_transcript->verticalScrollBar()->setValue(m_transcript->verticalScrollBar()->maximum());
}

void GoodyWindow::promptPermission(const QString &tool, const QString &argsSummary,
                                   const std::function<void(bool, bool)> &onDecide)
{
    m_permCallback = onDecide;
    m_permText->setText(QStringLiteral("Goody 请求执行 <b>%1</b><br>%2")
                            .arg(escapeHtml(tool), escapeHtml(argsSummary)));
    m_permBar->show();
    renderTranscript();
}

void GoodyWindow::setBusy(bool on)
{
    m_busy = on;
    m_sendBtn->setText(on ? QStringLiteral("停止") : QStringLiteral("发送"));
    m_input->setEnabled(true); // busy 时仍可输入下一条，但 Enter 不发送
    // busy（LLM 请求在途）同样禁用「清空」（C-N2 第一道保险）：清空后迟到
    // 回包 unconditional append 进空会话，断裂序列会让下一轮请求 400。
    // 与工具期联合判断，避免两阶段交错时按钮态被单侧复位
    updateClearBtnState();
}

void GoodyWindow::setToolPhase(bool on)
{
    // 工具执行期（含权限确认等待）禁用「清空」（B-2）：会话清空后剩余工具
    // 结果继续回填会留下孤儿 tool 序列，之后每轮请求被 API 整包拒绝
    m_toolPhase = on;
    updateClearBtnState();
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

} // namespace poptools
