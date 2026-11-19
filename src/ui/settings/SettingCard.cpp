#include "SettingCard.h"

#include <QLabel>
#include <QHBoxLayout>

namespace poptools {

SettingCard::SettingCard(const QString &title, const QString &desc, QWidget *parent)
    : QWidget(parent)
{
    setObjectName("settingCard");
    setAttribute(Qt::WA_StyledBackground); // hover 底色经窗口 QSS 生效

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(16, 10, 16, 10);
    lay->setSpacing(16);

    // 左列：标题 + 可选描述（描述空则不建，行高自动收紧）
    m_title = new QLabel(title, this);
    m_title->setObjectName("settingCardTitle");
    auto *textCol = new QVBoxLayout;
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(2);
    textCol->addWidget(m_title);
    if (!desc.isEmpty()) {
        m_desc = new QLabel(desc, this);
        m_desc->setObjectName("settingCardDesc");
        m_desc->setWordWrap(true);
        textCol->addWidget(m_desc);
    }
    lay->addLayout(textCol, 1);
}

void SettingCard::setTrailingWidget(QWidget *widget)
{
    if (!widget || m_trailing)
        return;
    m_trailing = widget;
    qobject_cast<QHBoxLayout *>(layout())->addWidget(widget, 0, Qt::AlignVCenter);
}

SettingCardGroup::SettingCardGroup(const QString &title, QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);

    auto *titleLabel = new QLabel(title, this);
    titleLabel->setObjectName("settingGroupTitle");
    lay->addWidget(titleLabel);

    m_container = new QFrame(this);
    m_container->setObjectName("settingCardGroup");
    m_cardsLayout = new QVBoxLayout(m_container);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(0);
    lay->addWidget(m_container);
}

void SettingCardGroup::addCard(SettingCard *card)
{
    // 非首卡前插分隔线：1px、左缩进 16 对齐卡片文字起点（Win11 范式）
    if (m_cardCount > 0) {
        auto *sepWrap = new QWidget(m_container);
        sepWrap->setAttribute(Qt::WA_StyledBackground);
        sepWrap->setObjectName("settingCardSeparator");
        sepWrap->setFixedHeight(1);
        auto *sepLay = new QHBoxLayout(sepWrap);
        sepLay->setContentsMargins(16, 0, 0, 0);
        m_cardsLayout->addWidget(sepWrap);
    }
    m_cardsLayout->addWidget(card);
    ++m_cardCount;
}

} // namespace poptools
