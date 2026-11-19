#pragma once

#include <QFrame>
#include <QVBoxLayout>

class QLabel;

namespace poptools {

// ============================================================
// Win11 设置卡片组件（SettingCard / SettingCardGroup）。
//
// SettingCard = "标题 + 一行描述 | 右侧即时控件" 的设置行，置于组容器内
// 时底色透明（hover 浮现卡片悬停色）；单飞使用时自身即卡片底。
// SettingCardGroup = "分组标题 + 圆角容器"，容器内卡片以 1px 分隔线相隔
// （Win11 Settings 的分组容器范式，分隔线左缩进对齐文字起点）。
//
// 样式走窗口级样式表的 objectName 规则（P-022）：#settingCard /
// #settingCardTitle / #settingCardDesc / #settingCardGroup /
// #settingGroupTitle / #settingCardSeparator，色值由窗口 applyTheme
// 统一从 Fluent token 提供（见 FluentTheme.h）。
// ============================================================

// 设置行：标题（必填）+ 描述（可空，空则不建）+ 右侧尾随控件（可空）
class SettingCard : public QWidget
{
    Q_OBJECT
public:
    explicit SettingCard(const QString &title, const QString &desc = QString(),
                         QWidget *parent = nullptr);

    // 右侧尾随控件（组合框/开关/输入行等）；垂直居中，卡片高度随其伸展
    void setTrailingWidget(QWidget *widget);

    QLabel *titleLabel() const { return m_title; }

private:
    QLabel *m_title = nullptr;
    QLabel *m_desc = nullptr;
    QWidget *m_trailing = nullptr;
};

// 设置分组：标题在容器外，卡片收进圆角容器，卡间自动插分隔线
class SettingCardGroup : public QWidget
{
    Q_OBJECT
public:
    explicit SettingCardGroup(const QString &title, QWidget *parent = nullptr);

    void addCard(SettingCard *card); // 非首卡自动在前方插入 1px 分隔线

private:
    QFrame *m_container = nullptr;
    QVBoxLayout *m_cardsLayout = nullptr;
    int m_cardCount = 0;
};

} // namespace poptools
