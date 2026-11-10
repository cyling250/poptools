#include "DailyRecordJob.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

#include "../monitor/ActivityStore.h" // meta.json 的 version 常量口径
#include "DailyAggregator.h"
#include "LlmClient.h"
#include "../settings/Settings.h"

namespace poptools {

namespace {
constexpr int kDigestBudget = 12000; // 摘要字符预算

// 每日总结系统提示词：四节式工作记录
const char *kSummarySystemPrompt =
    "你是工作记录整理助手。根据用户当天在本机留下的活动日志摘要（窗口时间线、键入摘录、"
    "中文上屏摘录），推断并撰写一份《本日工作记录》。要求：\n"
    "1. 用中文、Markdown；推断要克制——只写日志能支撑的内容，不确定的用推测语气标注；\n"
    "2. 固定四节：## 时间线概览 / ## 关键工作 / ## 产出与进展 / ## 待办线索；\n"
    "3. 键入摘录可能含拼音、密码占位符●、快捷键噪声，过滤后再归纳，绝不原文罗列大段输入；\n"
    "4. 总长控制在 500 字以内，信息密度优先。\n"
    "直接输出记录本身，不要开场白。";

void setMetaSummarized(const QString &dayDir, bool summarized)
{
    // 读改写 meta.json（QSaveFile 原子；保留 version 等既有字段）
    QJsonObject meta{{"version", 1}, {"summarized", summarized}};
    QFile in(dayDir + QStringLiteral("/meta.json"));
    if (in.open(QIODevice::ReadOnly)) {
        const QJsonObject old = QJsonDocument::fromJson(in.readAll()).object();
        in.close();
        if (!old.isEmpty()) {
            meta = old;
            meta.insert(QStringLiteral("summarized"), summarized);
        }
    }
    QSaveFile out(dayDir + QStringLiteral("/meta.json"));
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));
        out.commit();
    }
}
} // namespace

DailyRecordJob::DailyRecordJob(const QString &monitorDir, const QString &goodyDir,
                               QObject *parent)
    : QObject(parent)
    , m_monitorDir(monitorDir)
    , m_goodyDir(goodyDir)
    , m_client(new LlmClient(this))
{
}

bool DailyRecordJob::isPendingDay(const QString &monitorDir, const QString &date)
{
    const QString dayDir = monitorDir + QLatin1Char('/') + date;
    bool hasData = false;
    for (const QString &stream : {QStringLiteral("timeline"), QStringLiteral("keys"),
                                  QStringLiteral("ocrtext")}) {
        QFile f(dayDir + QLatin1Char('/') + stream + QStringLiteral(".jsonl"));
        if (f.exists() && f.size() > 0) {
            hasData = true;
            break;
        }
    }
    if (!hasData)
        return false;
    QFile meta(dayDir + QStringLiteral("/meta.json"));
    if (!meta.open(QIODevice::ReadOnly))
        return true; // 无 meta = 肯定没总结过
    return !QJsonDocument::fromJson(meta.readAll())
                .object()
                .value(QStringLiteral("summarized"))
                .toBool(false);
}

void DailyRecordJob::processPending(bool includeToday,
                                    const std::function<void(int, const QString &)> &done)
{
    // 重入闸门（P2-8）：启动补账/定时/手动三条入口共用 m_queue/m_done，
    // 一轮在跑（异步 buildDigestAsync/LLM 阶段）时再触发会覆写队列与
    // 回调——统一拒绝
    if (m_busy) {
        done(0, QStringLiteral("上一轮总结还在进行中"));
        return;
    }
    const QDate today = QDate::currentDate();
    QDir dir(m_monitorDir);
    m_queue.clear();
    const QStringList entries =
        dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name); // 升序：老日期先补
    for (const QString &name : entries) {
        const QDate d = QDate::fromString(name, QStringLiteral("yyyy-MM-dd"));
        if (!d.isValid())
            continue;
        if (!includeToday && d >= today)
            continue; // 补账模式：今天还没过完，不总结
        if (isPendingDay(m_monitorDir, name))
            m_queue << name;
    }
    m_done = done;
    m_processed = 0;
    m_lastError.clear();
    m_busy = true;
    runNext();
}

void DailyRecordJob::runForDate(const QString &date,
                                const std::function<void(bool, const QString &)> &done)
{
    // 与 processPending 共用同一闸门：m_client->busy 只覆盖 LLM 在途，
    // buildDigestAsync（线程池）阶段不体现在 busy 上
    if (m_busy || m_client->busy()) {
        done(false, QStringLiteral("上一个总结还在生成中"));
        return;
    }
    if (Settings::instance().assistantApiKey().trimmed().isEmpty()) {
        done(false, QStringLiteral("未配置 API Key（设置 → Goody 管家）"));
        return;
    }
    const QString dayDir = m_monitorDir + QLatin1Char('/') + date;
    m_busy = true;
    DailyAggregator::buildDigestAsync(dayDir, kDigestBudget, this,
                                      [this, date, done](bool ok, const QString &digest) {
                                          if (!ok) {
                                              m_busy = false; // 提前失败：复位闸门
                                              done(false, QStringLiteral("该日无活动日志"));
                                              return;
                                          }
                                          m_done = [done](int, const QString &err) {
                                              done(err.isEmpty(), err);
                                          };
                                          m_processed = 0;
                                          m_currentDate = date;
                                          summarizeDay(date, digest);
                                      });
}

void DailyRecordJob::runNext()
{
    if (m_queue.isEmpty()) {
        m_busy = false; // 回调链终点：整轮收束，复位重入闸门
        if (m_done)
            m_done(m_processed, m_lastError);
        return;
    }
    if (Settings::instance().assistantApiKey().trimmed().isEmpty()) {
        m_lastError = QStringLiteral("未配置 API Key");
        m_queue.clear();
        if (m_done)
            m_done(m_processed, m_lastError);
        return;
    }
    m_currentDate = m_queue.takeFirst();
    const QString dayDir = m_monitorDir + QLatin1Char('/') + m_currentDate;
    DailyAggregator::buildDigestAsync(
        dayDir, kDigestBudget, this,
        [this](bool ok, const QString &digest) {
            if (!ok) {
                m_lastError = QStringLiteral("%1 无数据").arg(m_currentDate);
                runNext();
                return;
            }
            summarizeDay(m_currentDate, digest);
        });
}

void DailyRecordJob::summarizeDay(const QString &date, const QString &digest)
{
    const Settings &s = Settings::instance();
    QString userPrompt =
        QStringLiteral("日期：%1\n\n以下是当天的活动日志摘要，请生成本日工作记录。\n\n%2")
            .arg(date, digest);
    // 允许总结今天（定时触发）：提示当天还没过完，时间线只覆盖到当前
    if (date == QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")))
        userPrompt += QStringLiteral("\n（注意：这是今天的进行时日志，只总结到当前时刻。）");

    QJsonArray messages;
    messages.append(QJsonObject{{"role", QStringLiteral("system")},
                                {"content", QString::fromUtf8(kSummarySystemPrompt)}});
    messages.append(QJsonObject{{"role", QStringLiteral("user")}, {"content", userPrompt}});

    const bool sent = m_client->chat(
        s.assistantApiBaseUrl(), s.assistantApiKey(), s.assistantApiModel(), messages);
    if (!sent) {
        finishDay(false, QStringLiteral("总结请求未能发出"));
        return;
    }
    connect(m_client, &LlmClient::finished, this,
            [this, date](bool ok, const QJsonObject &msg, const QString &error) {
                disconnect(m_client, &LlmClient::finished, this, nullptr);
                if (!ok) {
                    finishDay(false, QStringLiteral("%1 总结失败：%2").arg(date, error));
                    return;
                }
                const QString md = msg.value(QStringLiteral("content")).toString();
                if (md.trimmed().isEmpty()) {
                    finishDay(false, QStringLiteral("%1 模型返回空内容").arg(date));
                    return;
                }
                // 写 goody/daily/<date>.md（KB 级、每日一次，主线程可接受）
                QDir().mkpath(m_goodyDir + QStringLiteral("/daily"));
                QSaveFile f(m_goodyDir + QStringLiteral("/daily/") + date +
                            QStringLiteral(".md"));
                if (!f.open(QIODevice::WriteOnly)) {
                    finishDay(false, QStringLiteral("写入失败：%1").arg(f.errorString()));
                    return;
                }
                f.write(QStringLiteral("# 本日工作记录（%1）\n\n")
                            .arg(date)
                            .toUtf8());
                f.write(md.toUtf8());
                if (!f.commit()) {
                    finishDay(false, QStringLiteral("提交失败"));
                    return;
                }
                setMetaSummarized(m_monitorDir + QLatin1Char('/') + date, true);
                finishDay(true, QString());
            });
}

void DailyRecordJob::finishDay(bool ok, const QString &error)
{
    ++m_processed;
    if (!ok)
        m_lastError = error;
    runNext();
}

} // namespace poptools
