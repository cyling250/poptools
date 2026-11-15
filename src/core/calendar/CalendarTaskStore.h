#pragma once

#include <QDate>
#include <QHash>
#include <QString>
#include <QVector>

class QJsonObject;

namespace poptools {

struct CalendarTaskItem
{
    QString text;
    bool done = false;
    QString time; // 空 = 无时间前缀
};

// ============================================================
// 日程存储（照搬 rlqd CalendarClone/AppSettings.cs 的 TaskStore）：
// { "yyyy-MM-dd": [ {Text, Done, Time} ] }，兼容旧格式
// {"All":[{"Key":"yyyy-MM-dd","Value":[...]}]}；空日期自动移除。
// ============================================================
class CalendarTaskStore
{
public:
    static CalendarTaskStore load();
    // 解析任意 JSON（导入/加载共用）；失败返回 false
    bool fromJson(const QString &json);
    void save() const;
    QString exportJson() const;

    void mergeFrom(const CalendarTaskStore &other); // 同键整表替换 + 落盘

    QVector<CalendarTaskItem> get(const QDate &date) const;
    void set(const QDate &date, const QVector<CalendarTaskItem> &tasks); // 空表删除 + 落盘
    bool hasTasks(const QDate &date) const;

    // 日期键 → 条目列表（无序；搜索界面自行按日期降序排）
    const QHash<QString, QVector<CalendarTaskItem>> &all() const { return m_days; }

    static QString keyFor(const QDate &date) { return date.toString(QStringLiteral("yyyy-MM-dd")); }
    static bool isDateKey(const QString &key);

private:
    QHash<QString, QVector<CalendarTaskItem>> m_days;
};

} // namespace poptools
