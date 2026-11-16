#include "BoxCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

namespace poptools {

namespace {
constexpr int kVersion = 2;
QString s_dirOverride; // 单测注入；空串 = 默认路径
} // namespace

// 单盒 → JSON 对象（字段口径见 BoxDef 成员注释；x/y 为主屏物理像素）
QJsonObject BoxDef::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("dir")] = dir;
    o[QStringLiteral("x")] = x;
    o[QStringLiteral("y")] = y;
    o[QStringLiteral("cols")] = cols;
    o[QStringLiteral("page")] = page;
    o[QStringLiteral("hidden")] = hidden;
    return o;
}

// JSON → 单盒；id 或 dir 为空视为坏条目（ok=false 返回默认值，调用方跳过）；
// cols/page 就地钳制，容忍越界旧档
BoxDef BoxDef::fromJson(const QJsonObject &o, bool *ok)
{
    if (ok)
        *ok = false;
    BoxDef d;
    d.id = o.value(QStringLiteral("id")).toString();
    d.name = o.value(QStringLiteral("name")).toString();
    d.dir = o.value(QStringLiteral("dir")).toString();
    if (d.id.isEmpty() || d.dir.isEmpty())
        return d;
    d.x = o.value(QStringLiteral("x")).toInt(d.x);
    d.y = o.value(QStringLiteral("y")).toInt(d.y);
    d.cols = std::clamp(o.value(QStringLiteral("cols")).toInt(d.cols), 2, 8);
    d.page = std::max(0, o.value(QStringLiteral("page")).toInt(d.page));
    d.hidden = o.value(QStringLiteral("hidden")).toBool(d.hidden);
    if (ok)
        *ok = true;
    return d;
}

QString BoxCatalog::dataDir()
{
    if (!s_dirOverride.isEmpty())
        return s_dirOverride;
    return QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA"))
        + QStringLiteral("/PopTools/box");
}

QString BoxCatalog::storePath()
{
    return dataDir() + QStringLiteral("/boxes.json");
}

void BoxCatalog::setDataDirOverride(const QString &dir)
{
    s_dirOverride = dir;
}

QString BoxCatalog::managedDirFor(const QString &name)
{
    // 托管目录：boxes/<name>，重名自动 -2/-3 顺延，绝不并目录
    QString base = name.isEmpty() ? QStringLiteral("收纳盒") : name;
    QDir boxesDir(dataDir() + QStringLiteral("/boxes"));
    QString candidate = base;
    int n = 2;
    while (boxesDir.exists(candidate)) {
        candidate = QStringLiteral("%1-%2").arg(base).arg(n);
        ++n;
    }
    return boxesDir.absoluteFilePath(candidate);
}

// 仓库 → JSON 文档（v2 格式）
QJsonObject BoxCatalog::toJson() const
{
    QJsonObject root;
    root[QStringLiteral("version")] = kVersion;
    QJsonArray arr;
    for (const BoxDef &d : m_boxes)
        arr.append(d.toJson());
    root[QStringLiteral("boxes")] = arr;
    return root;
}

// JSON 文档 → 仓库（v2 格式）；解析失败不改动既有 m_boxes（先解析后清理的次序）
bool BoxCatalog::fromJson(const QString &json)
{
    QJsonParseError err {};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    m_boxes.clear();
    const QJsonArray arr = doc.object().value(QStringLiteral("boxes")).toArray();
    for (const QJsonValue &v : arr) {
        bool ok = false;
        const BoxDef d = BoxDef::fromJson(v.toObject(), &ok);
        if (ok)
            m_boxes.append(d);
    }
    return true;
}

// 读 boxes.json：文件缺失 = 默认空仓；解析失败 = 原档改名 .bad 留证后空载
//（防坏档反复崩溃，绝不覆盖式修复）
BoxCatalog BoxCatalog::load()
{
    BoxCatalog catalog;
    QFile f(storePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QString content = QString::fromUtf8(f.readAll());
        if (!catalog.fromJson(content)) {
            // 坏档不覆盖式修复：先留证（.bad），再以默认空载继续
            f.close();
            const QString bad = storePath() + QStringLiteral(".bad");
            QFile::remove(bad);
            QFile::rename(storePath(), bad);
            catalog = BoxCatalog();
        }
    }
    return catalog;
}

// 全量落盘：QSaveFile 原子替换；数据目录不存在则先建。写失败静默
//（无弹窗路径，调用方为高频小数据）
void BoxCatalog::save() const
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

// 线性查 id 下标；-1 = 不存在（盒数量级小，线性足够）
int BoxCatalog::indexOf(const QString &id) const
{
    for (int i = 0; i < m_boxes.size(); ++i) {
        if (m_boxes.at(i).id == id)
            return i;
    }
    return -1;
}

QString BoxCatalog::addBox(BoxDef def)
{
    // 无花括号 id（老式枚举最稳：Qt5 起即有，各编译器无歧义）
    def.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    def.cols = std::clamp(def.cols, 2, 8);
    def.page = std::max(0, def.page);
    m_boxes.append(def);
    save();
    return def.id;
}

void BoxCatalog::removeBox(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    m_boxes.removeAt(i);
    save();
}

void BoxCatalog::renameBox(const QString &id, const QString &name)
{
    const int i = indexOf(id);
    if (i < 0 || name.isEmpty())
        return;
    m_boxes[i].name = name;
    save();
}

void BoxCatalog::updateBox(const QString &id, const BoxDef &def)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    BoxDef d = def;
    d.id = id; // id 不可变
    d.cols = std::clamp(d.cols, 2, 8);
    d.page = std::max(0, d.page);
    m_boxes[i] = d;
    save();
}

void BoxCatalog::setHiddenAll(bool hidden)
{
    for (BoxDef &d : m_boxes)
        d.hidden = hidden;
    save();
}

} // namespace poptools
