#pragma once

#include <QDate>
#include <QDialog>
#include <QVector>

class QLineEdit;
class QListWidget;

namespace poptools {

class CalendarTaskStore;

// 内容搜索（照搬 rlqd SearchForm）：按日程文本过滤，选中条目跳转日期。
class CalendarSearchForm : public QDialog
{
    Q_OBJECT
public:
    explicit CalendarSearchForm(const CalendarTaskStore &store, QWidget *parent = nullptr);

    // 仅"打开"accept 路径写入；取消 / 未选中返回无效日期
    QDate selectedDate() const { return m_selectedDate; }

private slots:
    void applyTheme(); // Fluent token 主题应用（构造末尾 + 系统明暗切换）

private:
    // 全量重建列表：日期键降序、逐条文本过滤，无匹配时占位行
    void rebuild();

    const CalendarTaskStore &m_store;
    QLineEdit *m_query = nullptr;
    QListWidget *m_list = nullptr;
    QVector<QDate> m_dates;
    QDate m_selectedDate;
};

// 选择日期（照搬 rlqd DatePickerForm）：QCalendarWidget 单选 + 确定/取消。
class CalendarDatePickerForm : public QDialog
{
    Q_OBJECT
public:
    explicit CalendarDatePickerForm(const QDate &initial, QWidget *parent = nullptr);

    QDate selectedDate() const { return m_selectedDate; }

private slots:
    void applyTheme(); // Fluent token 主题应用（构造末尾 + 系统明暗切换）

private:
    QDate m_selectedDate;
};

} // namespace poptools
