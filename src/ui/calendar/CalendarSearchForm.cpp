#include "CalendarSearchForm.h"

#include "core/calendar/CalendarTaskStore.h"
#include "ui/ThemeColors.h"
#include "ui/theming/FluentTheme.h"

#include <QCalendarWidget>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStyleHints>
#include <QVBoxLayout>

#include <algorithm>

namespace poptools {

CalendarSearchForm::CalendarSearchForm(const CalendarTaskStore &store, QWidget *parent)
    : QDialog(parent)
    , m_store(store)
{
    setObjectName("calendarSearchDialog");
    setWindowTitle(QStringLiteral("内容搜索"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    setFixedSize(480, 480);
    // 2026-11-19 收编：原硬编码深色十六进制 → Fluent token，跟随系统明暗
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, &CalendarSearchForm::applyTheme);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 16, 16, 16);
    lay->setSpacing(10);

    auto *label = new QLabel(QStringLiteral("搜索日程内容："), this);
    lay->addWidget(label);
    m_query = new QLineEdit(this);
    lay->addWidget(m_query);
    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(false);
    lay->addWidget(m_list, 1);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    auto *open = new QPushButton(QStringLiteral("打开"), this);
    auto *cancel = new QPushButton(QStringLiteral("取消"), this);
    buttonRow->addWidget(open);
    buttonRow->addWidget(cancel);
    lay->addLayout(buttonRow);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(open, &QPushButton::clicked, this, [this] {
        const int i = m_list->currentRow();
        if (i >= 0 && i < m_dates.size()) {
            m_selectedDate = m_dates.at(i);
            accept();
        }
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this, open](QListWidgetItem *) { open->click(); });
    connect(m_query, &QLineEdit::textChanged, this, [this] { rebuild(); });
    rebuild();
    applyTheme();
}

// Fluent token 主题（2026-11-19 收编，方案 docs/design/
// 20261119-ui-fluent-restyle-proposal.md）：窗口基底/控件基线/列表取 token，
// Qt 内置类走类型选择器（P-022），无 palette 钉扎（SettingsDialog 同款口径）
void CalendarSearchForm::applyTheme()
{
    const FluentTokens t = fluentTheme(systemSchemeDark());
    setStyleSheet(QStringLiteral("#calendarSearchDialog { background: %1; }"
                                 "QLabel { color: %2; font-size: 14px; }")
                      .arg(t.bgBase, t.textPrimary) +
                  fluentInputQss(t) + fluentButtonQss(t) +
                  QStringLiteral("QListWidget { background: %1; color: %2;"
                                 "  border: 1px solid %3; border-radius: 4px; }"
                                 "QListWidget::item { padding: 5px 8px;"
                                 "  border-radius: 3px; }"
                                 "QListWidget::item:selected { background: %4;"
                                 "  color: %5; }")
                      .arg(t.bgCard, t.textPrimary, t.cardStroke, t.accentSoft,
                           t.textPrimary) +
                  fluentScrollBarQss(t));
    poptools::applyFluentWindowChrome(this, t.dark);
}

void CalendarSearchForm::rebuild()
{
    m_list->clear();
    m_dates.clear();
    const QString q = m_query->text().trimmed();
    // 日期键降序（rlqd OrderByDescending）
    QStringList keys;
    for (auto it = m_store.all().constBegin(); it != m_store.all().constEnd(); ++it)
        keys << it.key();
    std::sort(keys.begin(), keys.end(), std::greater<QString>());
    for (const QString &key : keys) {
        const QDate date = QDate::fromString(key, QStringLiteral("yyyy-MM-dd"));
        if (!date.isValid())
            continue;
        const QVector<CalendarTaskItem> tasks = m_store.all().value(key);
        for (const auto &t : tasks) {
            if (q.isEmpty() || t.text.contains(q, Qt::CaseInsensitive)) {
                m_list->addItem(
                    QStringLiteral("%1  %2").arg(key, t.text));
                m_dates.append(date);
            }
        }
    }
    if (m_list->count() == 0)
        m_list->addItem(QStringLiteral("（没有匹配的日程）"));
}

CalendarDatePickerForm::CalendarDatePickerForm(const QDate &initial, QWidget *parent)
    : QDialog(parent)
{
    setObjectName("calendarDatePickerDialog");
    setWindowTitle(QStringLiteral("选择日期"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    setFixedSize(340, 340);
    // 2026-11-19 收编：原硬编码深色十六进制 → Fluent token，跟随系统明暗
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, &CalendarDatePickerForm::applyTheme);

    auto *lay = new QVBoxLayout(this);
    auto *calendar = new QCalendarWidget(this);
    calendar->setGridVisible(true);
    calendar->setSelectedDate(initial);
    lay->addWidget(calendar, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, calendar] {
        m_selectedDate = calendar->selectedDate();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    applyTheme();
}

// Fluent token 主题（2026-11-19 收编）：QCalendarWidget 底色/导航栏/选中态
// 走 token；按钮基线复用 fluentButtonQss
void CalendarDatePickerForm::applyTheme()
{
    const FluentTokens t = fluentTheme(systemSchemeDark());
    setStyleSheet(QStringLiteral("#calendarDatePickerDialog { background: %1; }"
                                 "QCalendarWidget QWidget {"
                                 "  alternate-background-color: %2; }"
                                 "QCalendarWidget QAbstractItemView {"
                                 "  background: %2; color: %3;"
                                 "  selection-background-color: %4;"
                                 "  selection-color: %3; }"
                                 "QCalendarWidget QWidget#qt_calendar_navigationbar {"
                                 "  background: %1; }"
                                 "QCalendarWidget QToolButton { color: %3;"
                                 "  background: %1; border-radius: 4px; padding: 2px 6px; }"
                                 "QCalendarWidget QToolButton:hover { background: %5; }"
                                 "QCalendarWidget QToolButton::menu-indicator {"
                                 "  image: none; }")
                      .arg(t.bgBase, t.bgCard, t.textPrimary, t.accentSoft,
                           t.ctrlHover) +
                  fluentButtonQss(t));
    applyFluentWindowChrome(this, t.dark);
}

} // namespace poptools
