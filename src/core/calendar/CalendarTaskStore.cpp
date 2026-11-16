#include "CalendarTaskStore.h"

#include "CalendarSettings.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <algorithm>

namespace poptools {

namespace {

QJsonArray tasksToArray(const QVector<CalendarTaskItem> &tasks)
{
    QJsonArray arr;
    for (const auto &t : tasks) {
        QJsonObject o;
        o.insert("Text", t.text);
        o.insert("Done", t.done);
        if (!t.time.isEmpty())
            o.insert("Time", t.time);
        arr.append(o);
    }
    return arr;
}

QVector<CalendarTaskItem> tasksFromArray(const QJsonValue &value)
{
    QVector<CalendarTaskItem> out;
    if (!value.isArray())
        return out;
    for (const auto &v : value.toArray()) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        CalendarTaskItem t;
        t.text = o.value("Text").toString();
        t.done = o.value("Done").toBool(false);
        t.time = o.value("Time").toString();
        out.append(t);
    }
    return out;
}

} // namespace

bool CalendarTaskStore::isDateKey(const QString &key)
{
    if (key.size() != 10)
        return false;
    const QDate d = QDate::fromString(key, QStringLiteral("yyyy-MM-dd"));
    return d.isValid();
}

bool CalendarTaskStore::fromJson(const QString &json)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    QHash<QString, QVector<CalendarTaskItem>> days;
    const QJsonObject root = doc.object();
    const QJsonValue allValue = root.value("All");
    if (allValue.isArray()) {
        // 旧格式（DesktopCal 导出）：{"All":[{"Key":"yyyy-MM-dd","Value":[...]}]}
        for (const auto &entry : allValue.toArray()) {
            if (!entry.isObject())
                continue;
            const QJsonObject o = entry.toObject();
            const QString key = o.value("Key").toString();
            if (!isDateKey(key))
                continue;
            const QVector<CalendarTaskItem> tasks = tasksFromArray(o.value("Value"));
            days.insert(key, tasks);
        }
    } else {
        for (auto it = root.begin(); it != root.end(); ++it) {
            if (!isDateKey(it.key()))
                continue;
            days.insert(it.key(), tasksFromArray(it.value()));
        }
    }
    m_days = days;
    return true;
}

CalendarTaskStore CalendarTaskStore::load()
{
    CalendarTaskStore store;
    QFile f(CalendarSettings::tasksPath());
    if (!f.open(QIODevice::ReadOnly))
        return store;
    store.fromJson(QString::fromUtf8(f.readAll()));
    return store;
}

// 全库序列化（save/exportJson 共用）：同一遍历顺序、同一序列化器，输出字节一致
QJsonObject CalendarTaskStore::toJsonObject() const
{
    QJsonObject root;
    for (auto it = m_days.constBegin(); it != m_days.constEnd(); ++it)
        root.insert(it.key(), tasksToArray(it.value()));
    return root;
}

void CalendarTaskStore::save() const
{
    QFile f(CalendarSettings::tasksPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(toJsonObject()).toJson(QJsonDocument::Indented));
}

QString CalendarTaskStore::exportJson() const
{
    return QString::fromUtf8(QJsonDocument(toJsonObject()).toJson(QJsonDocument::Indented));
}

void CalendarTaskStore::mergeFrom(const CalendarTaskStore &other)
{
    for (auto it = other.m_days.constBegin(); it != other.m_days.constEnd(); ++it)
        m_days.insert(it.key(), it.value());
    save();
}

QVector<CalendarTaskItem> CalendarTaskStore::get(const QDate &date) const
{
    return m_days.value(keyFor(date));
}

void CalendarTaskStore::set(const QDate &date, const QVector<CalendarTaskItem> &tasks)
{
    const QString key = keyFor(date);
    if (tasks.isEmpty())
        m_days.remove(key);
    else
        m_days.insert(key, tasks);
    save();
}

bool CalendarTaskStore::hasTasks(const QDate &date) const
{
    return m_days.value(keyFor(date)).size() > 0;
}

} // namespace poptools
