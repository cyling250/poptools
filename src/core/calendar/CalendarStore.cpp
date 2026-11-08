#include "CalendarStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace poptools {

CalendarStore::CalendarStore(QObject *parent)
    : CalendarStore(QString(), parent)
{
}

CalendarStore::CalendarStore(const QString &jsonPath, QObject *parent)
    : QObject(parent)
{
    if (jsonPath.isEmpty()) {
        // 数据文件：AppData/Local/PopTools/calendar.json（标准可写目录；
        // Program Files 安装形态下 exe 旁无写权限，不走 PopTools.ini 的 exe 旁约定）
        const QString base = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
        m_filePath = base + "/calendar.json";
    } else {
        m_filePath = jsonPath;
    }
}

void CalendarStore::load()
{
    m_categories.clear();
    m_items.clear();

    QFile file(m_filePath);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        const QJsonObject root = doc.object();
        fromJson(root.value("categories").toArray(), root.value("items").toArray());
    }
    // 文件缺失/损坏时播种默认分类，保证可用
    ensureDefaultCategories();
}

void CalendarStore::save()
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("日历数据文件不可写：%s", qUtf8Printable(m_filePath));
        return;
    }

    QJsonObject root;
    QJsonArray items, categories;
    toJson(&items, &categories);
    root.insert("version", 1);
    root.insert("categories", categories);
    root.insert("items", items);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit())
        qWarning("日历数据写入失败：%s", qUtf8Printable(m_filePath));
}

// ---------- 条目 ----------

QList<CalendarItem> CalendarStore::itemsForDate(const QDate &date) const
{
    QList<CalendarItem> result;
    for (const CalendarItem &it : m_items)
        if (it.date == date)
            result << it;
    return result;
}

QString CalendarStore::addItem(const QDate &date, const QString &categoryId,
                               const QString &text, bool important)
{
    CalendarItem it;
    it.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    it.date = date;
    it.categoryId = categoryId.isEmpty() ? defaultCategoryId() : categoryId;
    it.text = text;
    it.important = important;
    m_items.append(it);
    save();
    emit changed();
    return it.id;
}

bool CalendarStore::updateItem(const QString &id, const QString &text,
                               const QString &categoryId, bool important)
{
    for (CalendarItem &it : m_items) {
        if (it.id != id)
            continue;
        it.text = text;
        it.categoryId = categoryId.isEmpty() ? defaultCategoryId() : categoryId;
        it.important = important;
        save();
        emit changed();
        return true;
    }
    return false;
}

bool CalendarStore::removeItem(const QString &id)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id != id)
            continue;
        m_items.removeAt(i);
        save();
        emit changed();
        return true;
    }
    return false;
}

bool CalendarStore::setDone(const QString &id, bool done)
{
    for (CalendarItem &it : m_items) {
        if (it.id != id || it.done == done)
            continue;
        it.done = done;
        save();
        emit changed();
        return true;
    }
    return false;
}

bool CalendarStore::moveItemToDate(const QString &id, const QDate &date)
{
    for (CalendarItem &it : m_items) {
        if (it.id != id || !date.isValid() || it.date == date)
            continue;
        it.date = date;
        save();
        emit changed();
        return true;
    }
    return false;
}

bool CalendarStore::moveItem(const QString &id, int delta)
{
    if (delta != -1 && delta != 1)
        return false;

    // 定位条目本身
    int selfIndex = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id) {
            selfIndex = i;
            break;
        }
    }
    if (selfIndex < 0)
        return false;

    // 同日条目在 m_items 中的下标序列，交换相邻两项
    const CalendarItem self = m_items.at(selfIndex);
    QVector<int> group;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).date == self.date)
            group.append(i);
    }
    const int target = group.indexOf(selfIndex);
    const int neighbor = target + delta;
    if (target < 0 || neighbor < 0 || neighbor >= group.size())
        return false;
    m_items.swapItemsAt(group.at(target), group.at(neighbor));
    save();
    emit changed();
    return true;
}

void CalendarStore::reorderItems(const QDate &date, const QStringList &orderedIds)
{
    // 取出该日现有序列（保持 m_items 相对顺序），按 orderedIds 重排
    QList<CalendarItem> group;
    for (const CalendarItem &it : m_items)
        if (it.date == date)
            group << it;

    QList<CalendarItem> reordered;
    for (const QString &id : orderedIds) {
        for (int i = 0; i < group.size(); ++i) {
            if (group.at(i).id == id) {
                reordered << group.takeAt(i);
                break;
            }
        }
    }
    reordered += group; // orderedIds 未覆盖的残项按原顺序垫底

    // 写回：遍历 m_items，命中组内位置时用重排后的序列逐个替换
    int cursor = 0;
    for (CalendarItem &it : m_items) {
        if (it.date == date && cursor < reordered.size())
            it = reordered.at(cursor++);
    }
    save();
    emit changed();
}

// ---------- 分类 ----------

QString CalendarStore::defaultCategoryId()
{
    return QStringLiteral("other");
}

void CalendarStore::ensureDefaultCategories()
{
    if (!m_categories.isEmpty())
        return;
    m_categories << CalendarCategory{ defaultCategoryId(), QStringLiteral("其他"),
                                      QColor(0x8A, 0x93, 0xA6) };
    m_categories << CalendarCategory{ QStringLiteral("work"), QStringLiteral("工作"),
                                      QColor(0x2B, 0x6F, 0xF2) };
    m_categories << CalendarCategory{ QStringLiteral("life"), QStringLiteral("生活"),
                                      QColor(0x22, 0xA0, 0x6B) };
    m_categories << CalendarCategory{ QStringLiteral("study"), QStringLiteral("学习"),
                                      QColor(0xF5, 0x9E, 0x0B) };
}

QString CalendarStore::addCategory(const QString &name, const QColor &color)
{
    if (name.trimmed().isEmpty())
        return QString();
    CalendarCategory cat;
    cat.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    cat.name = name.trimmed();
    cat.color = color.isValid() ? color : QColor(0x8A, 0x93, 0xA6);
    m_categories.append(cat);
    save();
    emit changed();
    return cat.id;
}

bool CalendarStore::updateCategory(const QString &id, const QString &name, const QColor &color)
{
    for (CalendarCategory &cat : m_categories) {
        if (cat.id != id)
            continue;
        if (!name.trimmed().isEmpty())
            cat.name = name.trimmed();
        if (color.isValid())
            cat.color = color;
        save();
        emit changed();
        return true;
    }
    return false;
}

bool CalendarStore::removeCategory(const QString &id)
{
    if (m_categories.size() <= 1)
        return false; // 至少保留一个分类
    if (id == defaultCategoryId())
        return false;

    bool removed = false;
    for (int i = 0; i < m_categories.size(); ++i) {
        if (m_categories.at(i).id == id) {
            m_categories.removeAt(i);
            removed = true;
            break;
        }
    }
    if (!removed)
        return false;

    // 引用该分类的条目改挂默认分类
    for (CalendarItem &it : m_items) {
        if (it.categoryId == id)
            it.categoryId = defaultCategoryId();
    }
    save();
    emit changed();
    return true;
}

QColor CalendarStore::categoryColor(const QString &id) const
{
    for (const CalendarCategory &cat : m_categories)
        if (cat.id == id)
            return cat.color;
    return QColor(0x8A, 0x93, 0xA6);
}

QString CalendarStore::categoryName(const QString &id) const
{
    for (const CalendarCategory &cat : m_categories)
        if (cat.id == id)
            return cat.name;
    return QStringLiteral("其他");
}

// ---------- JSON ----------

void CalendarStore::toJson(QJsonArray *items, QJsonArray *categories) const
{
    for (const CalendarCategory &cat : m_categories) {
        QJsonObject o;
        o.insert("id", cat.id);
        o.insert("name", cat.name);
        o.insert("color", cat.color.name(QColor::HexRgb));
        categories->append(o);
    }
    for (const CalendarItem &it : m_items) {
        QJsonObject o;
        o.insert("id", it.id);
        o.insert("date", it.date.toString(Qt::ISODate));
        o.insert("text", it.text);
        o.insert("category", it.categoryId);
        o.insert("important", it.important);
        o.insert("done", it.done);
        items->append(o);
    }
}

void CalendarStore::fromJson(const QJsonArray &categories, const QJsonArray &items)
{
    for (const QJsonValue &v : categories) {
        const QJsonObject o = v.toObject();
        CalendarCategory cat;
        cat.id = o.value("id").toString();
        cat.name = o.value("name").toString();
        cat.color = QColor(o.value("color").toString());
        if (!cat.id.isEmpty() && !cat.name.isEmpty() && cat.color.isValid())
            m_categories << cat;
    }
    // 注：旧版（T-042）条目带 todo 字段区分日程/待办，T-043 已合并为单一
    // 列表，读取时忽略该字段
    for (const QJsonValue &v : items) {
        const QJsonObject o = v.toObject();
        CalendarItem it;
        it.id = o.value("id").toString();
        it.date = QDate::fromString(o.value("date").toString(), Qt::ISODate);
        it.text = o.value("text").toString();
        it.categoryId = o.value("category").toString();
        it.important = o.value("important").toBool();
        it.done = o.value("done").toBool();
        if (!it.id.isEmpty() && it.date.isValid() && !it.text.isEmpty())
            m_items << it;
    }
}

} // namespace poptools
