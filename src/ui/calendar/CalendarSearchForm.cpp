#include "CalendarSearchForm.h"

#include "core/calendar/CalendarTaskStore.h"

#include <QCalendarWidget>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace poptools {

CalendarSearchForm::CalendarSearchForm(const CalendarTaskStore &store, QWidget *parent)
    : QDialog(parent)
    , m_store(store)
{
    setWindowTitle(QStringLiteral("内容搜索"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    setFixedSize(480, 480);
    setStyleSheet(QStringLiteral(
        "QDialog { background: #1E2026; color: #EBEBEB; font-size: 13px; }"
        "QLineEdit { background: #2D3038; color: #FFFFFF; border: 1px solid #3C4048; "
        "padding: 6px 8px; }"
        "QListWidget { background: #181A20; color: #EBEBEB; border: 1px solid #3C4048; }"
        "QPushButton { background: #383C46; color: #FFFFFF; border: 1px solid #5A606E; "
        "padding: 8px 24px; border-radius: 4px; }"));

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
    setWindowTitle(QStringLiteral("选择日期"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    setFixedSize(340, 340);
    setStyleSheet(QStringLiteral(
        "QDialog { background: #1E2026; color: #FFFFFF; font-size: 13px; }"
        "QCalendarWidget QWidget { alternate-background-color: #282A32; }"
        "QAbstractItemView { background: #282A32; color: #FFFFFF; "
        "selection-background-color: #383C46; }"
        "QPushButton { background: #383C46; color: #FFFFFF; border: 1px solid #5A606E; "
        "padding: 7px 20px; border-radius: 4px; }"));

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
}

} // namespace poptools
