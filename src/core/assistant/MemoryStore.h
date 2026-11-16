#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include "ToolRegistry.h"

namespace poptools {

class WorkingMemory;

// ============================================================
// 长期记忆（引用层叠式）：条目为 Markdown 文件，index.json 只存
// 引用与元数据（id/标签/类型/命中统计），正文按需读取——层与层之间
// 不复制内容：L1 用户偏好/事实、L2 项目脉络、L3 每日记录引用
//（指向 monitor/daily 的链接而非正文副本）。
//
// 检索：关键词匹配（标签 + 正文），命中即 touch（统计供过期维护）。
// 维护：goody 可经 memory.* 工具自行整理（合并/去重/过期删除），
// 周维护任务在 P6 之后接入。
// ============================================================
class MemoryStore : public QObject
{
    Q_OBJECT
public:
    // baseDir = goody 目录（记忆存 baseDir/memory/longterm/）
    explicit MemoryStore(const QString &baseDir, QObject *parent = nullptr);

    // 装载 index.json（正文缓存一并清空，防外部改动后陈旧）；文件缺失=
    // 首次使用空记忆，条目缺 id 跳过
    void load();

    // 新增条目，返回 id；kind: fact（偏好事实）/ project（项目脉络）/ ref（外部引用）
    QString addEntry(const QString &kind, const QStringList &tags, const QString &content);

    struct EntryMeta
    {
        QString id, kind;
        QStringList tags;
        qint64 created = 0;
        qint64 lastHit = 0;
        int hits = 0;
    };
    QList<EntryMeta> entries() const { return m_entries; }

    // 正文（按需读文件；不存在返回空）
    QString entryContent(const QString &id) const;

    // 关键词检索（标签/正文命中，命中即 touch）；query 为空时返回全部
    struct SearchHit
    {
        EntryMeta meta;
        QString excerpt; // 正文前 200 字
    };
    QList<SearchHit> search(const QString &query);

    bool removeEntry(const QString &id);

    // 索引摘要（注入系统提示词：每条一行 id/标签/首行）
    QString indexSummary(int maxEntries = 30) const;

private:
    QString entryPath(const QString &id) const;
    static QString makeId();

    QString m_dir;
    QList<EntryMeta> m_entries;
    // 正文缓存（id → 去头之后正文）：indexSummary 每回合读最多 30 个
    // 文件是主线程 IO（红线1），条目小而有限，读过的正文常驻缓存；
    // addEntry/removeEntry 同步维护
    mutable QHash<QString, QString> m_contentCache;
};

// 记忆/任务工具组（注册进 ToolRegistry；两者由 GoodyModule 持有）
QList<ToolDef> makeMemoryTools(MemoryStore *mem, WorkingMemory *work);

} // namespace poptools
