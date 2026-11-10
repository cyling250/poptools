#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

namespace poptools {

// ============================================================
// 瞬时工作记忆：当前长任务的挂起状态（goody/memory/working/<id>.json）。
//
// 语义（用户定义的"缓存命中"）：
//   * startTask 先按标题在未结任务中找相近项——命中即续用（continued=true），
//     不再另起新任务；模型借此把多次对话串在同一任务上；
//   * 任务上下文常驻（每次对话经系统提示词注入），AI 认为"当前瞬时任务
//     已完成"时调 task.complete：正文归档到 goody/memory/archive/
//     <date>-<title>.md 并从工作集移除（上下文即被释放）；
//   * 档案可被长期记忆引用（L3 层只存链接）。
// ============================================================
class WorkingMemory : public QObject
{
    Q_OBJECT
public:
    // baseDir = goody 目录（working/ 与 archive/ 在其下）
    explicit WorkingMemory(const QString &baseDir, QObject *parent = nullptr);

    // 开始任务：命中未结任务即续用（更新 goal），返回
    // {ok, id, continued:bool, title, notes:[...]}
    QJsonObject startTask(const QString &title, const QString &goal);

    // 未结任务清单（系统提示词注入 / task.list 工具）
    QJsonObject listTasks() const;

    // 追加进度笔记
    QJsonObject updateTask(const QString &id, const QString &note);

    // 完成并归档：返回 {ok, archived}；归档失败不影响任务移除
    QJsonObject completeTask(const QString &id, const QString &summary);

    // 系统提示词注入块（无未结任务返回空串）
    QString promptBlock() const;

    // 测试/维护：任务文件数
    int openCount() const { return m_tasks.size(); }

private:
    struct Task
    {
        QString id, title, goal;
        QStringList notes;
        qint64 created = 0, updated = 0;
    };
    void loadAll();
    bool saveTask(const Task &t) const;
    Task *find(const QString &id);
    static QString makeId();

    QString m_workDir, m_archiveDir;
    QList<Task> m_tasks; // 全部未结任务（启动时从磁盘装载）
};

} // namespace poptools
