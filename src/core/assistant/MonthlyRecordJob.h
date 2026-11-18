#pragma once

#include <QObject>
#include <QString>
#include <functional>

namespace poptools {

class LlmClient;

// ============================================================
// 月度工作摘要任务（T-067）：汇总某月 goody/daily/*.md 日摘要 → LLM
// 生成滚动月报 → 落盘 goody/monthly/<yyyy-MM>.md。
//
// 触发（用户拍板 2026-11-18）：
//   * 每次开机重写**当月**滚动月报（提示词含【月报更新】字样，进行时
//     口径：覆盖到生成时刻）——GoodyModule 装配，全程静默；
//   * 往月冻结不自动重算；重算指定月仅经用户与 Goody 对话触发
//     （app.generate_monthly_record 工具），无任何按钮。
//
// 数据口径：逐日正文是 LLM 自由文本（只保证 `# 本日工作记录（date）`
// 标题行），不逐节解析——整月原文拼接交一次 LLM（单日 2000 字截断，
// 总预算 24000 字，保序保头）。
// ============================================================
class MonthlyRecordJob : public QObject
{
    Q_OBJECT
public:
    MonthlyRecordJob(const QString &goodyDir, QObject *parent = nullptr);

    // 生成指定月份月报（重复生成允许：重写 MD）。done 主线程回调
    void runForMonth(int year, int month,
                     const std::function<void(bool ok, const QString &error)> &done);

    // 该月是否存在至少一份日摘要（开机免烧 LLM 的前置判断；静态供单测直测）
    static bool monthHasDailyData(const QString &goodyDir, int year, int month);

    // "2026-11" 形式的月份键
    static QString monthKey(int year, int month);
    // 月报落盘路径（存在性即"该月有没有月报"的判定口径）
    static QString monthPath(const QString &goodyDir, int year, int month);

private:
    QString m_goodyDir;
    LlmClient *m_client = nullptr; // 自建实例：与 DailyRecordJob 的单飞互不干扰
    bool m_busy = false;           // 重入闸门（开机触发与工具触发共用）
};

} // namespace poptools
