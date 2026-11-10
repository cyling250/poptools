#include "BoxStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
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
    if (!file.open(QIODevice::ReadOnly))
        return; // 首次使用：空仓库
    const QByteArray raw = file.readAll();
    file.close();

    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject()) {
        // 数据损坏（非 JSON/半截文件）：先把原件备份为 .bad 再空载——否则
        // 之后的任意一次 save 都会用空列表覆盖原文件，盒子清单永久丢失
        const QString badPath = m_filePath + QStringLiteral(".bad");
        QFile::remove(badPath);
        if (QFile::copy(m_filePath, badPath))
            qWarning("收纳盒数据损坏，原件已备份为 %s", qUtf8Printable(badPath));
        else
            qWarning("收纳盒数据损坏，且备份失败：%s", qUtf8Printable(m_filePath));
        return;
    }
    fromJson(doc.object().value("boxes").toArray());
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
    QString cleanDir;
    const QString dirInput = dir.trimmed();
    if (dirInput.isEmpty()) {
        cleanDir = QDir(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation))
                       .filePath(cleanName);
    } else {
        // 二次校验：目录参数须为绝对路径且不含 ".." 段——absolutePath()
        // 会把 .. 解析掉，穿越在下方 mkpath 时才显形，须在归一化前拦截
        if (QDir::isRelativePath(dirInput) || dirInput.contains(QLatin1String("..")))
            return QString();
        cleanDir = QDir(dirInput).absolutePath();
    }

    // 目录不存在时创建（存在则直接用——收纳已有的文件夹）；创建失败
    //（盘不可写/路径非法）返回空 id——调用方（托盘/工具）据此报失败
    if (!QFileInfo::exists(cleanDir) && !QDir().mkpath(cleanDir))
        return QString();

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

// 收纳移动的进程级互斥（C-N3）：不同 BoxWindow 的 worker 并发跑"uniqueTargetPath
// + 移动"时，各自可能看到同一空落点 → 撞车两败俱伤（一方失败气泡、文件不动）。
// 锁覆盖整段（含跨盘复制兜底），换来全进程收纳操作串行——用户操作频率低，
// 正确性优先
namespace {
QMutex g_fsMoveMutex;
} // namespace

// 单条目移动：rename 优先，失败（跨盘）走复制+删除兜底。纯 FS 操作，
// 无共享状态，可在工作线程调用
bool BoxStore::moveEntryIntoDir(const QFileInfo &info, const QString &dir)
{
    QMutexLocker lock(&g_fsMoveMutex);
    const QString target = BoxStore::uniqueTargetPath(dir, info.fileName());
    if (QFile::rename(info.filePath(), target))
        return true;
    return QFile::copy(info.filePath(), target) && QFile::remove(info.filePath());
}

QStringList BoxStore::moveUrlsIntoDir(const QString &dir, const QList<QUrl> &urls)
{
    QStringList failures;
    for (const QUrl &url : urls) {
        const QFileInfo info(url.toLocalFile());
        if (!info.exists() || !info.isFile())
            continue; // 目录条目/无效项跳过（盒子内可手动整理）
        if (!moveEntryIntoDir(info, dir))
            failures << info.fileName();
    }
    return failures;
}

QStringList BoxStore::moveDirContents(const QString &fromDir, const QString &toDir)
{
    QStringList failures;
    const QFileInfoList entries =
        QDir(fromDir).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                                    QDir::DirsFirst | QDir::Name);
    for (const QFileInfo &info : entries) {
        if (!moveEntryIntoDir(info, toDir))
            failures << info.fileName(); // 子目录跨盘无法复制兜底，与原行为一致
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
