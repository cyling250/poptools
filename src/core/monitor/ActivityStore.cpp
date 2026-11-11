#include "ActivityStore.h"

#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

namespace poptools {

namespace {
constexpr int kFlushThresholdLines = 64;  // 缓冲满 64 行立即移交（正常键入速率下约几秒的量）
constexpr int kFlushIntervalMs = 2000;    // 定时移交周期：崩溃最多丢 2s 缓冲
constexpr int kMetaVersion = 1;
} // namespace

// ============================================================
// Worker：全部磁盘操作在此执行（运行于 m_thread）
// ============================================================
class ActivityStoreWorker : public QObject
{
    Q_OBJECT
public:
    explicit ActivityStoreWorker(QString baseDir, int retentionDays)
        : m_baseDir(std::move(baseDir)), m_retentionDays(retentionDays)
    {
    }

public slots:
    // 追加一批行；跨天自动轮转（关闭旧句柄、建新日目录、写 meta）
    void write(const QString &stream, const QStringList &lines)
    {
        if (lines.isEmpty())
            return;
        const QDate today = QDate::currentDate();
        if (today != m_day) {
            closeAll();
            m_day = today;
        }
        QFile *f = m_files.value(stream);
        if (!f) {
            const QString dayDir = m_baseDir + QLatin1Char('/') + m_day.toString("yyyy-MM-dd");
            QDir().mkpath(dayDir);
            ensureMeta(dayDir);
            f = new QFile(dayDir + QLatin1Char('/') + stream + QStringLiteral(".jsonl"));
            if (!f->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                qWarning("ActivityStore: 打开 %s 失败：%s", qPrintable(f->fileName()),
                         qPrintable(f->errorString()));
                delete f;
                return;
            }
            m_files.insert(stream, f);
        }
        // 逐行补换行：JSON 编码已转义内容中的换行，join 拼接后仍是一行一事件
        QByteArray payload;
        for (const QString &line : lines) {
            payload += line.toUtf8();
            payload += '\n';
        }
        if (f->write(payload) == -1)
            qWarning("ActivityStore: 写入失败：%s", qPrintable(f->errorString()));
        f->flush();
        emit batchWritten();
    }

    // 保留期清理：只删 baseDir 下名字可解析为日期的子目录（误删护栏）
    void pruneOldDays()
    {
        if (m_retentionDays <= 0)
            return; // 0 = 永久保留
        const QDate cutoff = QDate::currentDate().addDays(-m_retentionDays);
        const QRegularExpression dateDir(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
        QDir base(m_baseDir);
        if (!base.exists())
            return;
        const QStringList dirs = base.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &name : dirs) {
            const QRegularExpressionMatch m = dateDir.match(name);
            if (!m.hasMatch())
                continue;
            const QDate d = QDate::fromString(name, "yyyy-MM-dd");
            if (d.isValid() && d < cutoff) {
                QDir(m_baseDir + QLatin1Char('/') + name)
                    .removeRecursively(); // 过期日志：用户已同意保留期自动清理
            }
        }
    }

signals:
    void batchWritten();

private:
    void closeAll()
    {
        for (QFile *f : m_files) {
            f->flush();
            f->close();
            delete f;
        }
        m_files.clear();
    }

    // 日目录首次创建时写 meta.json（已存在不覆盖——summarized 标志由
    // DailyRecordJob 维护，覆盖会丢补账进度）
    void ensureMeta(const QString &dayDir)
    {
        const QString path = dayDir + QStringLiteral("/meta.json");
        if (QFileInfo::exists(path))
            return;
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return;
        QJsonObject meta;
        meta.insert("version", kMetaVersion);
        meta.insert("summarized", false);
        f.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));
        f.commit();
    }

    QString m_baseDir;
    int m_retentionDays;
    QDate m_day; // 未开过文件时为无效日期 → 首写自动初始化
    QHash<QString, QFile *> m_files;
};

// ============================================================
// 主线程侧：缓冲 + 定时移交
// ============================================================
ActivityStore::ActivityStore(const QString &baseDir, QObject *parent)
    : QObject(parent)
    , m_baseDir(baseDir.isEmpty()
                    ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                          + QStringLiteral("/monitor")
                    : baseDir)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(kFlushIntervalMs);
    connect(m_timer, &QTimer::timeout, this, [this] { sendAll(); });
}

ActivityStore::~ActivityStore()
{
    stop();
}

void ActivityStore::start()
{
    if (m_running)
        return;
    m_running = true;
    m_thread = new QThread(this);
    m_worker = new ActivityStoreWorker(m_baseDir, m_retentionDays);
    m_worker->moveToThread(m_thread);
    connect(this, &ActivityStore::linesReady, m_worker, &ActivityStoreWorker::write);
    connect(m_worker, &ActivityStoreWorker::batchWritten, this, &ActivityStore::flushFinished);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();
    // 保留期清理交给 worker。用阻塞调用而非排队：stop() 的 quit 可能
    // 抢在排队事件之前生效（quit-before-exec 竞态），清理会被整体跳过；
    // 启动时清理量极小（目录扫描），阻塞毫秒级可接受
    QMetaObject::invokeMethod(m_worker, "pruneOldDays", Qt::BlockingQueuedConnection);
    m_timer->start();
}

void ActivityStore::stop()
{
    if (!m_running)
        return;
    m_timer->stop();
    // 余量落盘须在置 m_running=false 之前：flushBlocking/sendAll 都以
    // m_running 为守卫，先停会把最后一批缓冲（最多 2s / 64 行）挡在门外
    //（暂停监控前专门 flush 的残余恰好就是这批）
    flushBlocking();
    m_running = false;
    m_thread->quit();
    m_thread->wait();
    m_thread = nullptr;
    m_worker = nullptr;
}

void ActivityStore::append(const QString &stream, const QJsonObject &obj)
{
    m_bufs[stream] << QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    int total = 0;
    for (auto it = m_bufs.cbegin(); it != m_bufs.cend(); ++it)
        total += it->size();
    if (total >= kFlushThresholdLines)
        sendAll();
}

int ActivityStore::bufferedLines() const
{
    int total = 0;
    for (auto it = m_bufs.cbegin(); it != m_bufs.cend(); ++it)
        total += it->size();
    return total;
}

void ActivityStore::flushSoon()
{
    if (bufferedLines() > 0)
        sendAll();
}

void ActivityStore::flushBlocking()
{
    if (!m_running || bufferedLines() == 0)
        return;
    // 阻塞等 worker 写完这一批：flushFinished 由 batchWritten 转发
    QEventLoop loop;
    connect(this, &ActivityStore::flushFinished, &loop, &QEventLoop::quit,
            Qt::QueuedConnection);
    sendAll();
    // 兜底超时（正常路径由信号提前退出；慢盘下 2s 足够一批小数据写完）
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
}

void ActivityStore::sendAll()
{
    if (!m_running)
        return;
    // 全量流遍历：流名由写入方决定（monitor 三流 + goody 审计流 audit）。
    // 白名单遍历会让未列出的流静默滞留缓冲并被 clear 吞掉（audit 曾因此
    // 从未落盘）
    for (auto it = m_bufs.constBegin(); it != m_bufs.constEnd(); ++it) {
        if (!it->isEmpty())
            emit linesReady(it.key(), *it);
    }
    m_bufs.clear();
}

} // namespace poptools

#include "ActivityStore.moc"
