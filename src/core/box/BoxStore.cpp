#include "BoxStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

namespace poptools {

BoxStore::BoxStore(QObject *parent)
    : BoxStore(QString(), parent)
{
}

BoxStore::BoxStore(const QString &jsonPath, QObject *parent)
    : QObject(parent)
{
    if (jsonPath.isEmpty()) {
        // 数据文件：AppData/Local/PopTools/boxes.json（与 calendar.json 同约定）
        const QString base = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
        m_filePath = base + "/boxes.json";
    } else {
        m_filePath = jsonPath;
    }
}

void BoxStore::load()
{
    m_boxes.clear();

    QFile file(m_filePath);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        fromJson(doc.object().value("boxes").toArray());
    }
}

void BoxStore::save()
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("收纳盒数据文件不可写：%s", qUtf8Printable(m_filePath));
        return;
    }

    QJsonObject root;
    QJsonArray boxes;
    toJson(&boxes);
    root.insert("version", 1);
    root.insert("boxes", boxes);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit())
        qWarning("收纳盒数据写入失败：%s", qUtf8Printable(m_filePath));
}

// ---------- 盒子增删改 ----------

QString BoxStore::addBox(const QString &name, const QString &dir, int x, int y)
{
    const QString cleanName = name.trimmed().isEmpty()
                                  ? QStringLiteral("收纳盒")
                                  : name.trimmed();
    QString cleanDir = dir.trimmed().isEmpty()
                           ? QDir(QStandardPaths::writableLocation(
                                      QStandardPaths::DesktopLocation))
                                 .filePath(cleanName)
                           : QDir(dir.trimmed()).absolutePath();

    // 目录不存在时创建（存在则直接用——收纳已有的文件夹）
    if (!QFileInfo::exists(cleanDir))
        QDir().mkpath(cleanDir);

    // 名称去重（盒子标题，同目录名不再强绑）
    QSet<QString> used;
    for (const DesktopBox &b : m_boxes)
        used.insert(b.name);
    QString finalName = cleanName;
    for (int n = 2; used.contains(finalName); ++n)
        finalName = QStringLiteral("%1 %2").arg(cleanName).arg(n);

    DesktopBox box;
    box.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    box.name = finalName;
    box.dir = QDir::toNativeSeparators(cleanDir);
    box.x = x;
    box.y = y;
    m_boxes.append(box);
    save();
    emit changed();
    return box.id;
}

bool BoxStore::removeBox(const QString &id)
{
    // 只移除盒子（桌面框框），磁盘目录与其中的文件原样保留
    for (int i = 0; i < m_boxes.size(); ++i) {
        if (m_boxes.at(i).id != id)
            continue;
        m_boxes.removeAt(i);
        save();
        emit changed();
        return true;
    }
    return false;
}

bool BoxStore::renameBox(const QString &id, const QString &name)
{
    if (name.trimmed().isEmpty())
        return false;
    for (DesktopBox &box : m_boxes) {
        if (box.id != id)
            continue;
        box.name = name.trimmed();
        save();
        emit changed();
        return true;
    }
    return false;
}

bool BoxStore::setBoxPos(const QString &id, int x, int y)
{
    for (DesktopBox &box : m_boxes) {
        if (box.id != id)
            continue;
        if (box.x == x && box.y == y)
            return true;
        box.x = x;
        box.y = y;
        save();
        emit changed();
        return true;
    }
    return false;
}

bool BoxStore::setBoxCols(const QString &id, int cols)
{
    const int clamped = qBound(1, cols, 8);
    for (DesktopBox &box : m_boxes) {
        if (box.id != id)
            continue;
        if (box.cols == clamped)
            return true;
        box.cols = clamped;
        save();
        emit changed();
        return true;
    }
    return false;
}

bool BoxStore::setBoxRows(const QString &id, int rows)
{
    const int clamped = qBound(1, rows, 8);
    for (DesktopBox &box : m_boxes) {
        if (box.id != id)
            continue;
        if (box.rows == clamped)
            return true;
        box.rows = clamped;
        save();
        emit changed();
        return true;
    }
    return false;
}

bool BoxStore::setBoxCollapsed(const QString &id, bool collapsed)
{
    for (DesktopBox &box : m_boxes) {
        if (box.id != id)
            continue;
        if (box.collapsed == collapsed)
            return true;
        box.collapsed = collapsed;
        save();
        emit changed();
        return true;
    }
    return false;
}

// ---------- 解散 ----------

// 解散：目录内容全部移回桌面，随后删除盒子与腾空的目录。
// 有条目移动失败时不删盒子（文件留在原处），返回失败清单。
QStringList BoxStore::dissolveBox(const QString &id)
{
    for (const DesktopBox &box : m_boxes) {
        if (box.id != id)
            continue;

        const QString desktop = QStandardPaths::writableLocation(
            QStandardPaths::DesktopLocation);
        QDir dir(box.dir);
        QStringList failures;
        const QFileInfoList entries =
            dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                              QDir::DirsFirst | QDir::Name);
        for (const QFileInfo &info : entries) {
            const QString target = uniqueTargetPath(desktop, info.fileName());
            if (QFile::rename(info.filePath(), target))
                continue;
            // 跨盘移动：复制 + 删除兜底
            if (QFile::copy(info.filePath(), target) && QFile::remove(info.filePath()))
                continue;
            failures << info.fileName();
        }
        if (!failures.isEmpty())
            return failures; // 有失败：盒子与剩余文件原样保留

        // 目录已腾空 → 删空目录（删除失败时仅遗留空文件夹，无害不删）
        QDir().rmdir(box.dir);

        removeBox(id); // save + changed
        return {};
    }
    return {};
}

// ---------- 文件收纳操作 ----------

QString BoxStore::uniqueTargetPath(const QString &dir, const QString &fileName)
{
    const QString target = QDir(dir).filePath(fileName);
    if (!QFileInfo::exists(target))
        return target;
    const QFileInfo info(fileName);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix();
    for (int n = 2;; ++n) {
        const QString candidate =
            suffix.isEmpty() ? QStringLiteral("%1 (%2)").arg(base).arg(n)
                             : QStringLiteral("%1 (%2).%3").arg(base).arg(n).arg(suffix);
        const QString next = QDir(dir).filePath(candidate);
        if (!QFileInfo::exists(next))
            return next;
    }
}

QStringList BoxStore::moveUrlsIntoDir(const QString &dir, const QList<QUrl> &urls)
{
    QStringList failures;
    for (const QUrl &url : urls) {
        const QString path = url.toLocalFile();
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile())
            continue; // 目录条目/无效项跳过（盒子内可手动整理）
        const QString target = uniqueTargetPath(dir, info.fileName());
        if (QFile::rename(path, target))
            continue;
        // 跨盘移动：rename 失败时复制 + 删除兜底
        if (QFile::copy(path, target) && QFile::remove(path))
            continue;
        failures << info.fileName();
    }
    return failures;
}

// ---------- JSON ----------

void BoxStore::toJson(QJsonArray *boxes) const
{
    for (const DesktopBox &box : m_boxes) {
        QJsonObject o;
        o.insert("id", box.id);
        o.insert("name", box.name);
        o.insert("dir", box.dir);
        o.insert("x", box.x);
        o.insert("y", box.y);
        o.insert("cols", box.cols);
        o.insert("rows", box.rows);
        o.insert("collapsed", box.collapsed);
        boxes->append(o);
    }
}

void BoxStore::fromJson(const QJsonArray &boxes)
{
    for (const QJsonValue &v : boxes) {
        const QJsonObject o = v.toObject();
        DesktopBox box;
        box.id = o.value("id").toString();
        box.name = o.value("name").toString();
        box.dir = o.value("dir").toString();
        box.x = o.value("x").toInt(100);
        box.y = o.value("y").toInt(100);
        box.cols = qBound(1, o.value("cols").toInt(4), 8);
        box.rows = qBound(1, o.value("rows").toInt(4), 8);
        box.collapsed = o.value("collapsed").toBool();
        if (!box.id.isEmpty() && !box.name.isEmpty() && !box.dir.isEmpty())
            m_boxes << box;
    }
}

} // namespace poptools
