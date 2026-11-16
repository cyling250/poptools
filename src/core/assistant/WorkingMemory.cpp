#include "WorkingMemory.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace poptools {

WorkingMemory::WorkingMemory(const QString &baseDir, QObject *parent)
    : QObject(parent)
    , m_workDir(baseDir + QStringLiteral("/memory/working"))
    , m_archiveDir(baseDir + QStringLiteral("/memory/archive"))
{
    loadAll();
}

// 启动装载全部 working/*.json，只收 status=="open"（完成/异常态不装载）；
// 文件坏/缺 id 跳过
void WorkingMemory::loadAll()
{
    m_tasks.clear();
    QDir dir(m_workDir);
    if (!dir.exists())
        return;
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files);
    for (const QFileInfo &fi : files) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        if (o.value(QStringLiteral("status")).toString() != QLatin1String("open"))
            continue; // 完成/异常态不装载
        Task t;
        t.id = o.value(QStringLiteral("id")).toString();
        t.title = o.value(QStringLiteral("title")).toString();
        t.goal = o.value(QStringLiteral("goal")).toString();
        for (const QJsonValue &n : o.value(QStringLiteral("notes")).toArray())
            t.notes << n.toString();
        t.created = static_cast<qint64>(o.value(QStringLiteral("created")).toDouble());
        t.updated = static_cast<qint64>(o.value(QStringLiteral("updated")).toDouble());
        if (!t.id.isEmpty())
            m_tasks << t;
    }
}

// task_ + 毫秒低 8 位十六进制。T-062 修复: 同 MemoryStore::makeId 的同毫秒
// 撞号隐患（并行 tool_calls 逐个同步执行）——追加进程内自增序号后缀保证
// 唯一；新 id 仍只含十六进制字符，任务文件名白名单口径不变
QString WorkingMemory::makeId()
{
    static quint32 s_seq = 0;
    return QStringLiteral("task_%1%2")
        .arg(QDateTime::currentMSecsSinceEpoch() % 100000000, 8, 16, QLatin1Char('0'))
        .arg(++s_seq, 2, 16, QLatin1Char('0'));
}

// 单任务原子写（QSaveFile，status 恒 open——归档即删文件）；写失败返回
// false 由调用方决定是否提示
bool WorkingMemory::saveTask(const Task &t) const
{
    QDir().mkpath(m_workDir);
    const QJsonArray notes = QJsonArray::fromStringList(t.notes); // T-062: 装配样板去重
    QSaveFile f(m_workDir + QLatin1Char('/') + t.id + QStringLiteral(".json"));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(QJsonObject{
                 {"version", 1},
                 {"id", t.id},
                 {"title", t.title},
                 {"goal", t.goal},
                 {"notes", notes},
                 {"status", QStringLiteral("open")},
                 {"created", double(t.created)},
                 {"updated", double(t.updated)}})
                .toJson(QJsonDocument::Compact));
    return f.commit();
}

// 线性查 id，无则 nullptr
WorkingMemory::Task *WorkingMemory::find(const QString &id)
{
    for (Task &t : m_tasks)
        if (t.id == id)
            return &t;
    return nullptr;
}

QJsonObject WorkingMemory::startTask(const QString &title, const QString &goal)
{
    const QString t = title.trimmed();
    if (t.isEmpty())
        return QJsonObject{{"ok", false}, {"error", QStringLiteral("标题为空")}};
    // 缓存命中：标题包含匹配（"继续翻译" 命中 "翻译整理"）→ 续用
    for (Task &task : m_tasks) {
        if (task.title.contains(t, Qt::CaseInsensitive)
            || t.contains(task.title, Qt::CaseInsensitive)) {
            if (!goal.trimmed().isEmpty() && goal != task.goal)
                task.notes << QStringLiteral("[目标更新] ") + goal.trimmed();
            task.updated = QDateTime::currentMSecsSinceEpoch();
            saveTask(task);
            const QJsonArray notes = QJsonArray::fromStringList(task.notes); // T-062: 装配样板去重
            return QJsonObject{{"ok", true},
                               {"continued", true},
                               {"id", task.id},
                               {"title", task.title},
                               {"notes", notes},
                               {"note", QStringLiteral("命中未结任务，已续用（避免重复开工）")}};
        }
    }
    Task nt;
    nt.id = makeId();
    nt.title = t;
    nt.goal = goal.trimmed();
    nt.created = nt.updated = QDateTime::currentMSecsSinceEpoch();
    m_tasks << nt;
    saveTask(nt);
    return QJsonObject{{"ok", true},
                       {"continued", false},
                       {"id", nt.id},
                       {"title", nt.title}};
}

QJsonObject WorkingMemory::listTasks() const
{
    QJsonArray arr;
    for (const Task &t : m_tasks) {
        const QJsonArray notes = QJsonArray::fromStringList(t.notes); // T-062: 装配样板去重
        arr.append(QJsonObject{{"id", t.id},
                               {"title", t.title},
                               {"goal", t.goal},
                               {"notes", notes},
                               {"updated", double(t.updated)}});
    }
    return QJsonObject{{"ok", true}, {"tasks", arr}};
}

QJsonObject WorkingMemory::updateTask(const QString &id, const QString &note)
{
    Task *t = find(id);
    if (!t)
        return QJsonObject{{"ok", false}, {"error", QStringLiteral("任务不存在或已归档")}};
    t->notes << note.trimmed();
    t->updated = QDateTime::currentMSecsSinceEpoch();
    // 注意：此处 "notes" 是条数（number），startTask/listTasks 里同名键是
    // 数组——历史形状，模型侧靠上下文区分；改名属模型可见行为变更，只
    // 注释不改（T-062 登记：LLM 解析歧义，无崩溃路径）
    return QJsonObject{{"ok", saveTask(*t)}, {"notes", double(t->notes.size())}};
}

QJsonObject WorkingMemory::completeTask(const QString &id, const QString &summary)
{
    Task *t = find(id);
    if (!t)
        return QJsonObject{{"ok", false}, {"error", QStringLiteral("任务不存在或已归档")}};

    // 归档：标题日期化 + 摘要 + 全部笔记（供长期记忆 L3 引用）
    QDir().mkpath(m_archiveDir);
    QString safeTitle;
    for (const QChar &c : t->title) // 文件名清洗：非法字符转下划线
        safeTitle += (c.isLetterOrNumber() || c == QLatin1Char(' ')
                      || c == QLatin1Char('_') || c == QLatin1Char('-'))
            ? c : QLatin1Char('_');
    safeTitle = safeTitle.trimmed().left(40);
    if (safeTitle.isEmpty())
        safeTitle = QStringLiteral("task");
    const QString stem = m_archiveDir + QLatin1Char('/')
        + QDate::currentDate().toString("yyyy-MM-dd") + QLatin1Char('-') + safeTitle;
    QString path = stem + QStringLiteral(".md");
    // 同日同名归档防覆盖（R4 遗留）：档案名已存在时追加 _HHmmss 时间戳；
    // 同一秒内极端连续归档再退避序号，保证先完成的归档不被静默覆盖
    if (QFileInfo::exists(path)) {
        const QString stamp = QDateTime::currentDateTime().toString("HHmmss");
        QString candidate = stem + QLatin1Char('_') + stamp + QStringLiteral(".md");
        for (int n = 2; QFileInfo::exists(candidate); ++n)
            candidate = stem + QLatin1Char('_') + stamp + QLatin1Char('_')
                + QString::number(n) + QStringLiteral(".md");
        path = candidate;
    }
    // 归档原子写（QSaveFile，B-9①）：直写曾半截留档；写失败时任务保留在
    // 工作集可重试，不再静默丢档（原实现 open 失败也照常移除并删任务文件）
    QSaveFile md(path);
    bool archived = false;
    if (md.open(QIODevice::WriteOnly | QIODevice::Text)) {
        md.write(QStringLiteral("# %1\n\n- 开始：%2\n- 结束：%3\n- 目标：%4\n\n## 收尾摘要\n\n%5\n\n## 过程笔记\n")
                     .arg(t->title,
                          QDateTime::fromMSecsSinceEpoch(t->created).toString("MM-dd HH:mm"),
                          QDateTime::currentDateTime().toString("MM-dd HH:mm"),
                          t->goal.isEmpty() ? QStringLiteral("（未记录）") : t->goal,
                          summary.trimmed().isEmpty() ? QStringLiteral("（无）") : summary.trimmed())
                     .toUtf8());
        for (const QString &n : t->notes)
            md.write(QStringLiteral("- %1\n").arg(n).toUtf8());
        archived = md.commit();
    }
    if (!archived) {
        return QJsonObject{{"ok", false},
                           {"error",
                            QStringLiteral("归档写入失败（任务已保留，可重试）：%1").arg(path)}};
    }

    // 从工作集移除（上下文释放）+ 删任务文件
    const QString filePath = m_workDir + QLatin1Char('/') + t->id + QStringLiteral(".json");
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks.at(i).id == t->id) {
            m_tasks.removeAt(i);
            break;
        }
    }
    QFile::remove(filePath);
    return QJsonObject{{"ok", true},
                       {"archived", path},
                       {"note", QStringLiteral("已归档并释放上下文")}};
}

QString WorkingMemory::promptBlock() const
{
    if (m_tasks.isEmpty())
        return QString();
    QStringList lines;
    for (const Task &t : m_tasks) {
        // 多参单次 arg（B-9②）：一次替换 %1-%4，此后不再对含用户文本的
        // 结果调 arg——标题/目标里的 %N 保持字面（链式 arg 曾把笔记数
        // 注入含 %4 的标题段）
        lines << QStringLiteral("- [%1] %2%3（进度笔记 %4 条）")
                     .arg(t.id, t.title,
                          t.goal.isEmpty() ? QString()
                                           : QStringLiteral("：") + t.goal.left(60),
                          QString::number(t.notes.size()));
    }
    return QStringLiteral(
        "当前挂起的工作任务（开工前先看这里，命中即续用 task.start；完成后 task.complete 归档释放）：\n%1")
        .arg(lines.join(QLatin1Char('\n')));
}

} // namespace poptools
