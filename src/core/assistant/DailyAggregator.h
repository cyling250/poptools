#pragma once

#include <QObject>
#include <QString>
#include <functional>

namespace poptools {

// ============================================================
// 每日活动摘要器：把一天的 JSONL 日志（timeline/keys/ocrtext）压缩成
// 一份能直接喂给 LLM 的文字摘要（时间线 + 键入摘录 + 中文上屏摘录）。
//
// 压缩策略（控 token 预算）：
//   * 时间线：相邻同 (app,title) 段合并（60s 切片还原成整段），
//     每行"09:12–09:47 活跃30分 键入421 code.exe — main.cpp"；
//   * 键入/OCR：按窗口分组，每组只留前 300 字符 + 总字数标注；
//   * 总量按 maxChars 截断（默认 12000 字符 ≈ 数千 token）。
//
// 文件读取在线程池（红线1：磁盘 IO 禁入主线程）；ctx 用于把结果
// 送回主线程（模块析构后结果静默丢弃）。
// ============================================================
class DailyAggregator : public QObject
{
    Q_OBJECT
public:
    static void buildDigestAsync(const QString &dayDir, int maxChars, QObject *ctx,
                                 const std::function<void(bool ok, QString digest)> &done);
};

} // namespace poptools
