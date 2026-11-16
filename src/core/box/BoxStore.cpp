#include "BoxStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

#include <algorithm>

namespace poptools {

namespace {
constexpr int kVersion = 1;
QString s_dirOverride; // 单测注入；空串 = 默认路径
} // namespace

QJsonObject BoxSettings::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("x")] = x;
    o[QStringLiteral("y")] = y;
    o[QStringLiteral("width")] = width;
    o[QStringLiteral("height")] = height;
    o[QStringLiteral("cols")] = cols;
    o[QStringLiteral("hidden")] = hidden;
    o[QStringLiteral("glassEffect")] = glassEffect;
    o[QStringLiteral("fontScale")] = fontScale;
    o[QStringLiteral("page")] = page;
    return o;
}

void BoxSettings::fromJson(const QJsonObject &o)
{
    x = o.value(QStringLiteral("x")).toInt(x);
    y = o.value(QStringLiteral("y")).toInt(y);
    width = o.value(QStringLiteral("width")).toInt(width);
    height = o.value(QStringLiteral("height")).toInt(height);
    cols = std::clamp(o.value(QStringLiteral("cols")).toInt(cols), 2, 8);
    hidden = o.value(QStringLiteral("hidden")).toBool(hidden);
    glassEffect = o.value(QStringLiteral("glassEffect")).toBool(glassEffect);
    fontScale = std::clamp(o.value(QStringLiteral("fontScale")).toInt(fontScale), 70, 160);
    page = std::max(0, o.value(QStringLiteral("page")).toInt(page));
}

QString BoxStore::dataDir()
{
    if (!s_dirOverride.isEmpty())
        return s_dirOverride;
    return QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA"))
        + QStringLiteral("/PopTools/box");
}

QString BoxStore::storePath()
{
    return dataDir() + QStringLiteral("/box.json");
}

void BoxStore::setDataDirOverride(const QString &dir)
{
    s_dirOverride = dir;
}

QJsonObject BoxStore::toJson() const
{
    QJsonObject root;
    root[QStringLiteral("version")] = kVersion;
    root[QStringLiteral("settings")] = m_settings.toJson();
    QJsonArray arr;
    for (const BoxEntry &e : m_entries) {
        QJsonObject o;
        o[QStringLiteral("name")] = e.name;
        o[QStringLiteral("target")] = e.target;
        arr.append(o);
    }
    root[QStringLiteral("entries")] = arr;
    return root;
}

bool BoxStore::fromJson(const QString &json)
{
    QJsonParseError err {};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("settings")))
        m_settings.fromJson(root.value(QStringLiteral("settings")).toObject());
    m_entries.clear();
    const QJsonArray arr = root.value(QStringLiteral("entries")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        BoxEntry e;
        e.name = o.value(QStringLiteral("name")).toString();
        e.target = o.value(QStringLiteral("target")).toString();
        if (e.target.isEmpty())
            continue;
        if (e.name.isEmpty())
            e.name = QFileInfo(e.target).completeBaseName();
        m_entries.append(e);
    }
    return true;
}

BoxStore BoxStore::load()
{
    BoxStore store;
    QFile f(storePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QString content = QString::fromUtf8(f.readAll());
        if (!store.fromJson(content)) {
            // 坏档不覆盖式修复：先留证（.bad），再以默认空载继续
            f.close();
            const QString bad = storePath() + QStringLiteral(".bad");
            QFile::remove(bad);
            QFile::rename(storePath(), bad);
            store = BoxStore();
        }
    }
    return store;
}

void BoxStore::save() const
{
    const QDir dir(dataDir());
    if (!dir.exists())
        QDir().mkpath(dataDir());
    QSaveFile f(storePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
    f.commit();
}

void BoxStore::addEntry(const BoxEntry &e)
{
    m_entries.append(e);
    save();
}

void BoxStore::removeEntry(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;
    m_entries.removeAt(index);
    save();
}

void BoxStore::moveEntry(int from, int to)
{
    const int n = m_entries.size();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return;
    const BoxEntry e = m_entries.takeAt(from);
    m_entries.insert(to, e);
    save();
}

void BoxStore::setEntryName(int index, const QString &name)
{
    if (index < 0 || index >= m_entries.size())
        return;
    m_entries[index].name = name;
    save();
}

void BoxStore::clearEntries()
{
    if (m_entries.isEmpty())
        return;
    m_entries.clear();
    save();
}

} // namespace poptools
