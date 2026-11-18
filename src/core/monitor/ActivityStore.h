#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;
class QThread;

namespace poptools {

class ActivityStoreWorker;

// ============================================================
// 活动日志存储：JSONL 追加写 + 按日轮转 + 保留期清理。
//
// 落盘布局（baseDir 注入，默认 AppLocalDataLocation/monitor，本机即
// %LOCALAPPDATA%\PopTools\PopTools\monitor\——org/app 双层嵌套）：
//   baseDir/2026-11-09/textlog.jsonl   文本镜像事件（T-066；keys 字母流
//                                      已移除，旧日 keys.jsonl 仍可查）
//   baseDir/2026-11-09/timeline.jsonl  前台窗口活动段
//   baseDir/2026-11-09/ocrtext.jsonl   OCR 增量文字
//   baseDir/2026-11-09/meta.json       {version, summarized} —— DailyRecordJob 的补账索引
//
// 线程纪律（P-T039 红线1：磁盘 IO 禁入主线程）：主线程只做内存缓冲
//（每 2s 或满 64 行批量移交），QFile 打开/写盘/轮转/清理全部在 worker
// QThread 中执行。整行以 UTF-8 JSON 文本追加，无需加锁。
//
// 测试注入口：baseDir 可指定临时目录；flushSoon() 触发立即移交，
// flushFinished() 信号用于 QTRY 同步断言。
// ============================================================
class ActivityStore : public QObject
{
    Q_OBJECT
public:
    // baseDir 为空时取 AppLocalDataLocation/monitor
    explicit ActivityStore(const QString &baseDir = QString(), QObject *parent = nullptr);
    ~ActivityStore() override;

    void start(); // 启动 worker 线程 + 刷盘定时器 + 保留期清理
    void stop();  // 余量落盘并停线程（析构自动调用）
    bool isRunning() const { return m_running; }

    // 保留天数（0 = 永久；须在 start 前设置）。默认 14
    void setRetentionDays(int days) { m_retentionDays = days; }

    // 缓冲一行事件（obj 序列化为紧凑 JSON）。超过阈值立即移交 worker
    void append(const QString &stream, const QJsonObject &obj);

    // 未落盘行数（测试/诊断用）
    int bufferedLines() const;

    // 立即把缓冲移交 worker（异步；完成后发 flushFinished）
    void flushSoon();

    // 同步等待 worker 把已有缓冲写完（stop 前调用 / 测试用）
    void flushBlocking();

signals:
    void linesReady(const QString &stream, const QStringList &lines); // → worker（排队）
    void flushFinished(); // 一批写完（测试 QTRY 断言用）

private:
    void sendAll(); // 缓冲 → worker

    // 各流缓冲行数求和：append 的阈值判定与 bufferedLines 共用
    int totalBuffered() const;

    QString m_baseDir;
    int m_retentionDays = 14;
    bool m_running = false;
    QHash<QString, QStringList> m_bufs;
    QTimer *m_timer = nullptr; // 2s 周期刷盘（主线程）
    QThread *m_thread = nullptr;
    ActivityStoreWorker *m_worker = nullptr; // 归 m_thread
};

} // namespace poptools
