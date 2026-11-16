#include "Scheduler.h"

#include <QDate>
#include <QDateTime>
#include <QTimer>

namespace poptools {

Scheduler::Scheduler(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(60 * 1000);
    connect(m_timer, &QTimer::timeout, this, &Scheduler::tick);
}

Scheduler::~Scheduler()
{
    stop();
}

void Scheduler::start()
{
    if (!m_timer->isActive())
        m_timer->start();
}

void Scheduler::stop()
{
    m_timer->stop();
}

void Scheduler::setDailyTask(const QString &name, const QTime &at,
                             const std::function<void()> &fn)
{
    if (!at.isValid() || !fn)
        return;
    // 重注册（改时刻/回调）保留当天已跑记账：设置页改时刻不应导致
    // 当天已跑过的任务重复触发
    const QDate lastRun = m_jobs.value(name).lastRunDate;
    m_jobs.insert(name, DailyJob{at, fn, lastRun});
}

void Scheduler::tick()
{
    const QDate today = QDate::currentDate();
    const QTime now = QTime::currentTime();
    // T-062 修复: 原先迭代 m_jobs 期间同步执行 job.fn()——fn 内任何一次
    // setDailyTask 重注册（注释明示支持"重注册=改时刻/回调"）会 insert 进
    // QHash，可能触发 rehash 使迭代器失效（UB）。当前内置 fn（processPending）
    // 全程异步故未触发，但这是未来"用户自定义提醒"接入后的必然踩雷点。
    // 改为先快照后执行：记账（lastRunDate=today）保留在第一循环内，"先记账
    // 再执行"防连发语义不变
    QList<std::function<void()>> due;
    for (DailyJob &job : m_jobs) {
        if (job.lastRunDate == today)
            continue; // 今天已跑
        if (now < job.at)
            continue;
        job.lastRunDate = today; // 先记账再执行：回调抛异常/重入不会连发
        due << job.fn;
    }
    for (const std::function<void()> &fn : due)
        fn();
}

} // namespace poptools
