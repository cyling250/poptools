#include "MonthlyRecordJob.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QThreadPool>

#include "LlmClient.h"
#include "../settings/Settings.h"

namespace poptools {

namespace {
constexpr int kPerDayChars = 2000;  // 单日摘要拼入上限
constexpr int kTotalBudget = 24000; // 整月拼接总预算（31 天 × 500 字日摘要 ≈ 15k，留余量）

// 月报系统提示词：【月报更新】为用户指定的标识字样（T-067）
const char *kMonthlySystemPrompt =
    "【月报更新】你是工作月报整理助手。用户给出某个月逐日的《本日工作记录》"
    "（Markdown），请汇总撰写该月的滚动月报。要求：\n"
    "1. 用中文、Markdown；跨日去重与归纳，只写日志能支撑的内容，不确定的"
    "用推测语气标注；\n"
    "2. 固定四节：## 本月概览 / ## 关键工作与进展 / ## 值得注意 / ## 下月线索；\n"
    "3. 总长控制在 600 字以内，信息密度优先。\n"
    "直接输出月报本身，不要开场白。";

// 整月逐日原文拼接（保序保头：截断优先舍尾部日子）。纯读盘无共享状态，
// 线程池可调——T-068：开机冷缓存下 31 次文件开读曾在主线程 ~380ms（探针
// 实录），顶成低级钩子停摆
QString buildMonthDigest(const QString &goodyDir, int year, int month)
{
    QString digest;
    const QDate first(year, month, 1);
    if (!first.isValid())
        return digest;
    const int days = first.daysInMonth();
    for (int d = 1; d <= days; ++d) {
        const QString date = MonthlyRecordJob::monthKey(year, month) + QLatin1Char('-')
                             + QString::number(d).rightJustified(2, QLatin1Char('0'));
        QFile f(goodyDir + QStringLiteral("/daily/") + date + QStringLiteral(".md"));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QString day = QString::fromUtf8(f.readAll());
        if (day.size() > kPerDayChars)
            day = day.left(kPerDayChars) + QStringLiteral("\n…（截断）");
        digest += QStringLiteral("\n---- %1 ----\n").arg(date) + day + QLatin1Char('\n');
        if (digest.size() > kTotalBudget) {
            digest += QStringLiteral("\n…（超出预算已截断）");
            break;
        }
    }
    return digest;
}
} // namespace

MonthlyRecordJob::MonthlyRecordJob(const QString &goodyDir, QObject *parent)
    : QObject(parent)
    , m_goodyDir(goodyDir)
    , m_client(new LlmClient(this))
{
}

QString MonthlyRecordJob::monthKey(int year, int month)
{
    return QStringLiteral("%1-%2")
        .arg(year, 4, 10, QLatin1Char('0'))
        .arg(month, 2, 10, QLatin1Char('0'));
}

QString MonthlyRecordJob::monthPath(const QString &goodyDir, int year, int month)
{
    return goodyDir + QStringLiteral("/monthly/") + monthKey(year, month)
           + QStringLiteral(".md");
}

bool MonthlyRecordJob::monthHasDailyData(const QString &goodyDir, int year, int month)
{
    const QDate first(year, month, 1);
    if (!first.isValid())
        return false;
    const int days = first.daysInMonth();
    for (int d = 1; d <= days; ++d) {
        const QString date = monthKey(year, month) + QLatin1Char('-')
                             + QString::number(d).rightJustified(2, QLatin1Char('0'));
        if (QFileInfo::exists(goodyDir + QStringLiteral("/daily/") + date
                              + QStringLiteral(".md")))
            return true;
    }
    return false;
}

void MonthlyRecordJob::runForMonth(int year, int month,
                                   const std::function<void(bool, const QString &)> &done)
{
    // 重入闸门（开机触发与对话工具触发共用一个 m_busy；T-068 起置位提前到
    // 聚合期——聚合也在飞行窗口内，防重入堆叠）
    if (m_busy || m_client->busy()) {
        done(false, QStringLiteral("上一次月报还在生成中"));
        return;
    }
    if (Settings::instance().assistantApiKey().trimmed().isEmpty()) {
        done(false, QStringLiteral("未配置 API Key（设置 → Goody 管家）"));
        return;
    }
    const QDate first(year, month, 1);
    if (!first.isValid()) {
        done(false, QStringLiteral("月份不合法"));
        return;
    }

    // T-068：31 天文件拼接挪线程池（开机冷缓存下主线程 ~380ms，探针实录，
    // 顶成低级钩子停摆）；聚合完回主线程续发 LLM 请求。done 随链路带回主
    // 线程回调（池线程只拷贝不调用）
    m_busy = true;
    const QString key = monthKey(year, month);
    const QString goodyDir = m_goodyDir;
    QPointer<MonthlyRecordJob> self(this);
    QThreadPool::globalInstance()->start([self, goodyDir, year, month, key, done] {
        const QString digest = buildMonthDigest(goodyDir, year, month);
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  [self, digest, key, year, month, done] {
                                      if (self)
                                          self->sendMonthlyRequest(year, month, key, digest, done);
                                  },
                                  Qt::QueuedConnection);
    });
}

// 主线程续段（聚合完成回投）：空摘要/发失败即回 done；发送成功后 LLM 回包
// 里写盘收尾。done 由本函数链路在主线程回调
void MonthlyRecordJob::sendMonthlyRequest(int year, int month, const QString &key,
                                          const QString &digest,
                                          const std::function<void(bool, const QString &)> &done)
{
    if (digest.trimmed().isEmpty()) {
        m_busy = false;
        done(false, QStringLiteral("该月无日摘要可汇总"));
        return;
    }
    QJsonArray messages;
    messages.append(QJsonObject{{"role", QStringLiteral("system")},
                                {"content", QString::fromUtf8(kMonthlySystemPrompt)}});
    QString userPrompt =
        QStringLiteral("月份：%1\n\n以下是该月逐日的工作记录，请生成本月滚动月报。\n\n%2")
            .arg(key, digest);
    if (QDate::currentDate().year() == year && QDate::currentDate().month() == month)
        userPrompt += QStringLiteral("\n（注意：这是当月的滚动月报，只总结到当前时刻。）");
    messages.append(QJsonObject{{"role", QStringLiteral("user")}, {"content", userPrompt}});

    const Settings &s = Settings::instance();
    const bool sent = m_client->chat(s.assistantApiBaseUrl(), s.assistantApiKey(),
                                     s.assistantApiModel(), messages);
    if (!sent) {
        m_busy = false;
        done(false, QStringLiteral("月报请求未能发出"));
        return;
    }
    connect(m_client, &LlmClient::finished, this,
            [this, key, year, month, done](bool ok, const QJsonObject &msg,
                                           const QString &error) {
                disconnect(m_client, &LlmClient::finished, this, nullptr);
                m_busy = false;
                if (!ok) {
                    done(false, QStringLiteral("%1 月报生成失败：%2").arg(key, error));
                    return;
                }
                const QString md = msg.value(QStringLiteral("content")).toString();
                if (md.trimmed().isEmpty()) {
                    done(false, QStringLiteral("%1 模型返回空内容").arg(key));
                    return;
                }
                // 写 goody/monthly/<yyyy-MM>.md（KB 级，主线程可接受）
                QDir().mkpath(m_goodyDir + QStringLiteral("/monthly"));
                QSaveFile f(monthPath(m_goodyDir, year, month));
                if (!f.open(QIODevice::WriteOnly)) {
                    done(false, QStringLiteral("写入失败：%1").arg(f.errorString()));
                    return;
                }
                f.write(QStringLiteral("# 月度工作摘要（%1）\n\n").arg(key).toUtf8());
                f.write(md.toUtf8());
                if (!f.commit()) {
                    done(false, QStringLiteral("提交失败"));
                    return;
                }
                done(true, QString());
            });
}

} // namespace poptools
