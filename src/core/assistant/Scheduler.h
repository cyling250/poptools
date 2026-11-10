#pragma once

#include <QDate>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTime>
#include <functional>

class QTimer;

namespace poptools {

// ============================================================
// goody 内置调度器：60s 心跳，驱动"每天到点跑一次"的内置任务
//（当前：本日工作记录生成；未来：记忆维护、用户自定义提醒）。
//
// 错过补偿交给任务自身：DailyRecordJob.processPending 扫描全部
// 待总结日，启动补账兜底，故此处不持久化 lastRun（内存即可）。
// ============================================================
class Scheduler : public QObject
{
    Q_OBJECT
public:
    explicit Scheduler(QObject *parent = nullptr);
    ~Scheduler() override;

    void start();
    void stop();

    // 注册每日任务（同一 name 重注册 = 改时刻/回调）。at 无效时刻不注册
    void setDailyTask(const QString &name, const QTime &at,
                      const std::function<void()> &fn);

public:
    // 手动驱动一拍（测试用；生产路径由内部 QTimer 槽调用）
    void tick();

private:
    struct DailyJob
    {
        QTime at;
        std::function<void()> fn;
        QDate lastRunDate; // 当天已跑则跳过（跨天重置）
    };
    QTimer *m_timer = nullptr;
    QHash<QString, DailyJob> m_jobs;
};

} // namespace poptools
