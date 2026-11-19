#include "ProcessRow.h"

#include "GoodyTheme.h"
#include "MessageBubble.h" // AutoHeightBrowser（气泡正文同款自动高度浏览器）

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>

namespace poptools {

namespace {
constexpr int kDotSize = 8;

// 状态徽章点 QSS：8px 圆，色随态（dsh state 徽章语义：运行中蓝/完成绿/失败红）
QString stateDotQss(ProcessRow::State state)
{
    QColor c;
    switch (state) {
    case ProcessRow::State::Running: c = goody_theme::accent(); break;
    case ProcessRow::State::Ok: c = goody_theme::success(); break;
    case ProcessRow::State::Error: c = goody_theme::error(); break;
    }
    return QStringLiteral("background: %1; border-radius: 4px;").arg(c.name());
}
} // namespace

ProcessRow::ProcessRow(const QString &toolName, const QString &summary, QWidget *parent)
    : QWidget(parent)
    , m_toolName(toolName)
    , m_summary(summary)
{
    setObjectName("goodyProcessRow");
    setAttribute(Qt::WA_StyledBackground);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *headLay = new QHBoxLayout;
    headLay->setContentsMargins(4, 4, 4, 4);
    headLay->setSpacing(8);

    m_dot = new QLabel(this);
    m_dot->setObjectName("goodyProcessDot");
    m_dot->setFixedSize(kDotSize, kDotSize);
    m_dot->setStyleSheet(stateDotQss(m_state));
    headLay->addWidget(m_dot, 0, Qt::AlignVCenter);

    m_head = new QToolButton(this);
    m_head->setObjectName("goodyProcessHead");
    m_head->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_head->setCursor(Qt::PointingHandCursor);
    m_head->setCheckable(true);
    m_head->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    headLay->addWidget(m_head, 1);
    lay->addLayout(headLay);

    m_result = new AutoHeightBrowser(this);
    m_result->setObjectName("goodyProcessResult");
    m_result->setHeightCap(200); // 结果展开体内部滚动阈值
    m_result->hide();
    lay->addWidget(m_result);

    connect(m_head, &QToolButton::toggled, this, [this](bool checked) {
        m_result->setVisible(checked);
    });
    refreshHead();
}

void ProcessRow::refreshHead()
{
    if (!m_head)
        return;
    // 头部文本：名称（Semibold 深色）+ 分隔点 + 摘要（次级灰，超长省略）
    m_head->setText(QStringLiteral(
                        "<span style='font-weight:600'>%1</span> <span style='color:%2'>· %3</span>")
                        .arg(m_toolName.toHtmlEscaped(),
                             goody_theme::textTertiary().name(),
                             m_summary.toHtmlEscaped()));
}

void ProcessRow::finish(State state, const QString &resultText)
{
    m_state = state;
    if (m_dot)
        m_dot->setStyleSheet(stateDotQss(state));
    if (m_result)
        m_result->setPlainText(resultText);
}

void ProcessRow::setSummary(const QString &summary)
{
    m_summary = summary;
    // 同步进动态属性：GoodyWindow::applyTheme 主题切换时经
    // property("goodySummary") 读回原摘要重抛（只写成员会让主题切换后
    // 所有过程行摘要清空成空串）
    setProperty("goodySummary", QVariant(summary));
    refreshHead();
}

} // namespace poptools
