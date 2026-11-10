#include "MemoryStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include "WorkingMemory.h"

namespace poptools {

MemoryStore::MemoryStore(const QString &baseDir, QObject *parent)
    : QObject(parent)
    , m_dir(baseDir + QStringLiteral("/memory/longterm"))
{
}

void MemoryStore::load()
{
    m_entries.clear();
    QFile f(m_dir + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::ReadOnly))
        return; // 首次使用：空记忆
    const QJsonArray arr =
        QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("entries")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        EntryMeta m;
        m.id = o.value(QStringLiteral("id")).toString();
        m.kind = o.value(QStringLiteral("kind")).toString();
        for (const QJsonValue &t : o.value(QStringLiteral("tags")).toArray())
            m.tags << t.toString();
        m.created = static_cast<qint64>(o.value(QStringLiteral("created")).toDouble());
        m.lastHit = static_cast<qint64>(o.value(QStringLiteral("lastHit")).toDouble());
        m.hits = o.value(QStringLiteral("hits")).toInt();
        if (!m.id.isEmpty())
            m_entries << m;
    }
}

void saveIndex(const QString &dir, const QList<MemoryStore::EntryMeta> &entries)
{
    QDir().mkpath(dir);
    QJsonArray arr;
    for (const MemoryStore::EntryMeta &m : entries) {
        QJsonArray tags;
        for (const QString &t : m.tags)
            tags.append(t);
        arr.append(QJsonObject{{"id", m.id},
                               {"kind", m.kind},
                               {"tags", tags},
                               {"created", double(m.created)},
                               {"lastHit", double(m.lastHit)},
                               {"hits", m.hits}});
    }
    QSaveFile f(dir + QStringLiteral("/index.json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{{"version", 1}, {"entries", arr}})
                    .toJson(QJsonDocument::Compact));
        f.commit();
    }
}

QString MemoryStore::makeId()
{
    return QStringLiteral("mem_%1").arg(QDateTime::currentMSecsSinceEpoch() % 100000000, 8, 16, QLatin1Char('0'));
}

QString MemoryStore::addEntry(const QString &kind, const QStringList &tags,
                              const QString &content)
{
    if (content.trimmed().isEmpty())
        return QString();
    const QString id = makeId();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 正文 MD（frontmatter 供人阅读；机器检索走 index）
    QDir().mkpath(m_dir + QStringLiteral("/entries"));
    QFile md(m_dir + QStringLiteral("/entries/") + id + QStringLiteral(".md"));
    if (md.open(QIODevice::WriteOnly | QIODevice::Text)) {
        md.write(QStringLiteral("---\nid: %1\nkind: %2\ntags: %3\ncreated: %4\n---\n\n%5\n")
                     .arg(id, kind, tags.join(QStringLiteral(",")),
                          QDateTime::fromMSecsSinceEpoch(now).toString("yyyy-MM-dd HH:mm"),
                          content.trimmed())
                     .toUtf8());
    }

    EntryMeta m;
    m.id = id;
    m.kind = kind;
    m.tags = tags;
    m.created = now;
    m_entries << m;
    saveIndex(m_dir, m_entries);
    return id;
}

QString MemoryStore::entryPath(const QString &id) const
{
    // id 白名单化（防路径穿越：只允许字母数字下划线）
    QString safe;
    for (const QChar &c : id)
        if (c.isLetterOrNumber() || c == QLatin1Char('_'))
            safe += c;
    return m_dir + QStringLiteral("/entries/") + safe + QStringLiteral(".md");
}

QString MemoryStore::entryContent(const QString &id) const
{
    QFile f(entryPath(id));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QString text = QString::fromUtf8(f.readAll());
    // 去掉 frontmatter（正文即 --- 之后内容）
    if (text.startsWith(QLatin1String("---"))) {
        const int end = text.indexOf(QLatin1String("---"), 3);
        if (end >= 0)
            text = text.mid(end + 3);
    }
    return text.trimmed();
}

QList<MemoryStore::SearchHit> MemoryStore::search(const QString &query)
{
    QList<SearchHit> hits;
    const QString q = query.trimmed();
    for (const EntryMeta &m : m_entries) {
        // 命中判定：query 任一空格分词命中标签即中（正文按需比对）
        bool tagHit = q.isEmpty();
        if (!tagHit) {
            const QStringList words = q.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (const QString &w : words) {
                for (const QString &tag : m.tags) {
                    if (tag.contains(w, Qt::CaseInsensitive)) {
                        tagHit = true;
                        break;
                    }
                }
                if (tagHit)
                    break;
            }
        }
        QString content;
        if (!tagHit && !q.isEmpty()) {
            content = entryContent(m.id);
            tagHit = content.contains(q, Qt::CaseInsensitive);
        }
        if (!tagHit)
            continue;
        if (content.isEmpty())
            content = entryContent(m.id);
        // touch：命中统计（引用层叠的"热度"）
        for (EntryMeta &e : m_entries) {
            if (e.id == m.id) {
                ++e.hits;
                e.lastHit = QDateTime::currentMSecsSinceEpoch();
            }
        }
        SearchHit h;
        h.meta = m;
        h.excerpt = content.left(200);
        hits << h;
    }
    saveIndex(m_dir, m_entries); // touch 落盘
    return hits;
}

bool MemoryStore::removeEntry(const QString &id)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id == id) {
            QFile::remove(entryPath(id));
            m_entries.removeAt(i);
            saveIndex(m_dir, m_entries);
            return true;
        }
    }
    return false;
}

QString MemoryStore::indexSummary(int maxEntries) const
{
    if (m_entries.isEmpty())
        return QStringLiteral("（暂无长期记忆）");
    QStringList lines;
    int n = 0;
    for (const EntryMeta &m : m_entries) {
        if (n >= maxEntries)
            break;
        // 首行正文摘要（读文件开销可接受：条目数有限）
        const QString content = entryContent(m.id);
        const QString firstLine =
            content.section(QLatin1Char('\n'), 0, 0).left(60);
        lines << QStringLiteral("- [%1] %2（%3）%4")
                     .arg(m.kind, m.id, m.tags.join(QStringLiteral("/")), firstLine);
        ++n;
    }
    return lines.join(QLatin1Char('\n'));
}

// ---- 工具组 ----

QList<ToolDef> makeMemoryTools(MemoryStore *mem, WorkingMemory *work)
{
    QList<ToolDef> tools;
    Q_ASSERT(mem && work);

    {
        ToolDef d;
        d.name = QStringLiteral("memory.save");
        d.readOnly = true; // 写 goody 自己的记忆库（非用户文件系统），安全
        d.description = QStringLiteral(
            "把重要事实存入长期记忆：用户偏好、项目背景、常用路径等。"
            "tags 用短标签（如 偏好/项目名/路径）。");
        d.parameters = QJsonObject{
            {"type", QStringLiteral("object")},
            {"properties",
             QJsonObject{{"content", QJsonObject{{"type", QStringLiteral("string")},
                                                 {"description", QStringLiteral("要记住的内容")}}},
                         {"tags", QJsonObject{{"type", QStringLiteral("array")},
                                              {"items", QJsonObject{{"type", QStringLiteral("string")}}},
                                              {"description", QStringLiteral("标签列表")}}},
                         {"kind", QJsonObject{{"type", QStringLiteral("string")},
                                              {"description", QStringLiteral("fact/project/ref，默认 fact")}}}}},
            {"required", QJsonArray{QStringLiteral("content")}},
        };
        d.fn = [mem](const QJsonObject &args, const std::function<void(QJsonObject)> &done) {
            QStringList tags;
            for (const QJsonValue &t : args.value(QStringLiteral("tags")).toArray())
                tags << t.toString();
            const QString id = mem->addEntry(args.value(QStringLiteral("kind")).toString(
                                                 QStringLiteral("fact")),
                                             tags,
                                             args.value(QStringLiteral("content")).toString());
            if (id.isEmpty())
                done(QJsonObject{{"ok", false}, {"error", QStringLiteral("内容为空")}});
            else
                done(QJsonObject{{"ok", true}, {"id", id}});
        };
        tools << d;
    }

    {
        ToolDef d;
        d.name = QStringLiteral("memory.search");
        d.readOnly = true;
        d.description = QStringLiteral("检索长期记忆（标签/正文关键词）；query 空则列出全部。");
        d.parameters = QJsonObject{
            {"type", QStringLiteral("object")},
            {"properties",
             QJsonObject{{"query", QJsonObject{{"type", QStringLiteral("string")}}}}}};
        d.fn = [mem](const QJsonObject &args, const std::function<void(QJsonObject)> &done) {
            const QList<MemoryStore::SearchHit> hits =
                mem->search(args.value(QStringLiteral("query")).toString());
            QJsonArray arr;
            for (const MemoryStore::SearchHit &h : hits)
                arr.append(QJsonObject{{"id", h.meta.id},
                                       {"kind", h.meta.kind},
                                       {"tags", QJsonArray::fromStringList(h.meta.tags)},
                                       {"excerpt", h.excerpt}});
            done(QJsonObject{{"ok", true}, {"hits", arr}});
        };
        tools << d;
    }

    {
        ToolDef d;
        d.name = QStringLiteral("memory.forget");
        d.readOnly = false; // 删除需确认
        d.description = QStringLiteral("删除一条长期记忆（先 search 拿 id）。");
        d.parameters = QJsonObject{
            {"type", QStringLiteral("object")},
            {"properties",
             QJsonObject{{"id", QJsonObject{{"type", QStringLiteral("string")}}}}},
            {"required", QJsonArray{QStringLiteral("id")}},
        };
        d.fn = [mem](const QJsonObject &args, const std::function<void(QJsonObject)> &done) {
            done(QJsonObject{{"ok", mem->removeEntry(args.value(QStringLiteral("id")).toString())}});
        };
        tools << d;
    }

    // ---- 瞬时工作记忆 ----

    {
        ToolDef d;
        d.name = QStringLiteral("task.start");
        d.readOnly = true;
        d.description = QStringLiteral(
            "开始一项工作（缓存命中语义）：先查未结任务，标题相近即续用原任务"
            "（返回 continued=true），否则新建。长任务期间用它挂起进度。");
        d.parameters = QJsonObject{
            {"type", QStringLiteral("object")},
            {"properties",
             QJsonObject{{"title", QJsonObject{{"type", QStringLiteral("string")}}},
                         {"goal", QJsonObject{{"type", QStringLiteral("string")}}}}},
            {"required", QJsonArray{QStringLiteral("title")}},
        };
        d.fn = [work](const QJsonObject &args, const std::function<void(QJsonObject)> &done) {
            done(work->startTask(args.value(QStringLiteral("title")).toString(),
                                 args.value(QStringLiteral("goal")).toString()));
        };
        tools << d;
    }

    {
        ToolDef d;
        d.name = QStringLiteral("task.list");
        d.readOnly = true;
        d.description = QStringLiteral("列出未结的瞬时任务。");
        d.parameters = QJsonObject{{"type", QStringLiteral("object")},
                                   {"properties", QJsonObject{}}};
        d.fn = [work](const QJsonObject &, const std::function<void(QJsonObject)> &done) {
            done(work->listTasks());
        };
        tools << d;
    }

    {
        ToolDef d;
        d.name = QStringLiteral("task.update");
        d.readOnly = true;
        d.description = QStringLiteral("给未结任务追加一条进度/笔记。");
        d.parameters = QJsonObject{
            {"type", QStringLiteral("object")},
            {"properties",
             QJsonObject{{"id", QJsonObject{{"type", QStringLiteral("string")}}},
                         {"note", QJsonObject{{"type", QStringLiteral("string")}}}}},
            {"required", QJsonArray{QStringLiteral("id"), QStringLiteral("note")}},
        };
        d.fn = [work](const QJsonObject &args, const std::function<void(QJsonObject)> &done) {
            done(work->updateTask(args.value(QStringLiteral("id")).toString(),
                                  args.value(QStringLiteral("note")).toString()));
        };
        tools << d;
    }

    {
        ToolDef d;
        d.name = QStringLiteral("task.complete");
        d.readOnly = true; // 归档是 goody 自己的记忆整理，非用户文件
        d.description = QStringLiteral(
            "完成任务并归档：写入 archive 并释放上下文（本任务从工作集移除，"
            "对话可另起新题）。完成后向用户简报归档位置。");
        d.parameters = QJsonObject{
            {"type", QStringLiteral("object")},
            {"properties",
             QJsonObject{{"id", QJsonObject{{"type", QStringLiteral("string")}}},
                         {"summary", QJsonObject{{"type", QStringLiteral("string")},
                                                 {"description", QStringLiteral("收尾摘要（写入档案）")}}}}},
            {"required", QJsonArray{QStringLiteral("id"), QStringLiteral("summary")}},
        };
        d.fn = [work](const QJsonObject &args, const std::function<void(QJsonObject)> &done) {
            done(work->completeTask(args.value(QStringLiteral("id")).toString(),
                                    args.value(QStringLiteral("summary")).toString()));
        };
        tools << d;
    }

    return tools;
}

} // namespace poptools
