#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

namespace poptools {

class LlmClient;

// ============================================================
// 本日工作记录任务：扫描活动监控日志中"有数据但未总结"的日期，
// 经 DailyAggregator 压缩 → LLM 生成四节式工作记录 MD → 落盘
// goody/daily/<date>.md，并置该日 meta.json summarized=true（补账索引）。
//
// 触发点（GoodyModule 装配）：
//   * 启动 5s 后补账（日期 < 今天的未总结日）；
//   * Scheduler 每日到点（含今天）；
//   * goody 工具 app.generate_daily_record（用户让管家手动生成）。
// 多天积压时按日期顺序逐个处理（LLM 单飞，不并发烧钱）。
// ============================================================
class DailyRecordJob : public QObject
{
    Q_OBJECT
public:
    DailyRecordJob(const QString &monitorDir, const QString &goodyDir, QObject *parent = nullptr);

    // 扫描并处理全部待总结日期（includeToday=false 时只补历史日）。
    // done(processed) 在全部完成后回调（主线程）
    void processPending(bool includeToday,
                        const std::function<void(int processed, const QString &lastError)> &done);

    // 指定某天生成一次（重复生成允许：重写 MD、meta 保持 true）
    void runForDate(const QString &date,
                    const std::function<void(bool ok, const QString &error)> &done);

    // 该日是否有日志数据且未总结（processPending 的扫描口径）
    static bool isPendingDay(const QString &monitorDir, const QString &date);

private:
    void runNext();
    void summarizeDay(const QString &date, const QString &digest);
    void finishDay(bool ok, const QString &error);

    QString m_monitorDir, m_goodyDir;
    LlmClient *m_client = nullptr;
    QStringList m_queue;      // 待处理日期（升序）
    QString m_currentDate;    // 在途日期
    std::function<void(int, const QString &)> m_done;
    int m_processed = 0;
    QString m_lastError;
};

} // namespace poptools
