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
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
        DailyJob &job = it.value();
        if (job.lastRunDate == today)
            continue; // 今天已跑
        if (now < job.at)
            continue;
        job.lastRunDate = today; // 先记账再执行：回调抛异常/重入不会连发
        job.fn();
    }
}

} // namespace poptools
