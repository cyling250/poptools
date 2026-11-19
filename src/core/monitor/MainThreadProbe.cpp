#include "MainThreadProbe.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>

#include <windows.h>

#include <algorithm>
#include <climits>

#ifndef POPTOOLS_VERSION
#define POPTOOLS_VERSION "dev"
#endif

namespace poptools {

namespace {

// 报告阈值与窗口常量（口径见需求单 requirements/20261119-mouse-stall-probe.md）
constexpr qint32 kHookDelayReportMs = 40;  // 钩子延迟报告阈值：事件→回调 ≥40ms 记异常
constexpr qint32 kPumpStallReportMs = 50;  // 泵心跳间隔 ≥50ms 记卡顿
constexpr qint32 kCtxBeforeS = 2;          // 异常事件上下文窗：前 2s
constexpr qint32 kCtxAfterS = 1;           //                        后 1s
constexpr int kCtxMaxRecs = 1500;          // 上下文记录数上限（超出按步长抽稀）
constexpr int kFlushMs = 2000;             // 换出周期
constexpr int kHeartbeatMs = 10;           // 泵心跳周期

QString resolveImageNameBasename(quint32 pid)
{
    // 只读查询进程映像名（PROCESS_QUERY_LIMITED_INFORMATION 不发窗口消息，
    // 不卡目标进程），落盘线程专用
    if (!pid)
        return QString();
    const HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return QString();
    wchar_t buf[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    QString name;
    if (QueryFullProcessImageNameW(h, 0, buf, &size))
        name = QString::fromWCharArray(buf);
    CloseHandle(h);
    const qsizetype slash = name.lastIndexOf(QLatin1Char('\\'));
    return slash >= 0 ? name.mid(slash + 1) : name;
}

} // namespace

// ============================================================
// 落盘线程：消费主线程换出的 Batch 写 JSONL。无 Q_OBJECT（纯 run 循环）。
// 文件按日滚动 probe-YYYYMMDD.jsonl；启动时写 probe-meta.json 记三时钟
// 起点（QPC/GetTickCount/墙钟），供与外部被动监听脚本日志对时。
// 定义在 poptools 命名空间（头文件前向声明的就是此类型，匿名命名空间
// 类型与 friend 声明不匹配）
// ============================================================
class ProbeWriterThread : public QThread
{
public:
    struct Meta {
        qint64 qpcFreq = 1;
        qint64 qpc0 = 0;
        quint32 tick0 = 0;
        QString epoch0;
        QString ver;
        QString exePath;
        quint32 pid = 0;
        QString dir;
        QString host;
    };

    explicit ProbeWriterThread(MainThreadProbe *probe, Meta meta)
        : m_probe(probe), m_meta(std::move(meta))
    {
    }

    void run() override
    {
        writeMeta();
        forever {
            std::deque<MainThreadProbe::Batch> local;
            {
                QMutexLocker lock(&m_probe->m_ioMutex);
                while (m_probe->m_ioQueue.empty() && !m_probe->m_writerQuit)
                    m_probe->m_ioCond.wait(&m_probe->m_ioMutex, 1000);
                local.swap(m_probe->m_ioQueue);
                if (m_probe->m_writerQuit && local.empty())
                    return;
            }
            for (auto &batch : local)
                writeBatch(batch);
        }
    }

private:
    void writeMeta()
    {
        QDir().mkpath(m_meta.dir);
        QJsonObject o;
        o.insert(QStringLiteral("k"), QStringLiteral("meta"));
        o.insert(QStringLiteral("ver"), m_meta.ver);
        o.insert(QStringLiteral("pid"), qint64(m_meta.pid));
        o.insert(QStringLiteral("exe"), m_meta.exePath);
        o.insert(QStringLiteral("qpcFreq"), qint64(m_meta.qpcFreq));
        o.insert(QStringLiteral("qpc0"), qint64(m_meta.qpc0));
        o.insert(QStringLiteral("tick0"), qint64(m_meta.tick0));
        o.insert(QStringLiteral("epoch0"), m_meta.epoch0);
        o.insert(QStringLiteral("thHookMs"), kHookDelayReportMs);
        o.insert(QStringLiteral("thPumpMs"), kPumpStallReportMs);
        o.insert(QStringLiteral("host"), m_meta.host);
        QFile f(m_meta.dir + QStringLiteral("/probe-meta.json"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            f.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
    }

    QFile *fileForNow()
    {
        const QDate today = QDate::currentDate();
        if (!m_file.isOpen() || today != m_fileDate) {
            if (m_file.isOpen())
                m_file.close();
            m_fileDate = today;
            m_file.setFileName(m_meta.dir + QStringLiteral("/probe-")
                               + today.toString(QStringLiteral("yyyyMMdd"))
                               + QStringLiteral(".jsonl"));
            if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
                return nullptr; // 落盘失败不致命：下个周期再试
        }
        return &m_file;
    }

    QString fgExe(quint32 pid)
    {
        if (!pid)
            return QString();
        auto it = m_fgExe.constFind(pid);
        if (it != m_fgExe.constEnd())
            return *it;
        const QString name = resolveImageNameBasename(pid);
        m_fgExe.insert(pid, name);
        return name;
    }

    void writeBatch(const MainThreadProbe::Batch &b)
    {
        QFile *f = fileForNow();
        if (!f)
            return;
        QByteArray out;
        out.reserve(512 + b.ctx.size() * 24 + b.hookIncidents.size() * 160
                    + b.pumpIncidents.size() * 128);
        auto appendLine = [&](const QByteArray &line) {
            out += line;
            out += '\n';
        };

        // 统计行：零异常周期也写——探针活体证据，区分"没有卡顿"与"探针死了"
        {
            QJsonObject o;
            o.insert(QStringLiteral("k"), QStringLiteral("stat"));
            o.insert(QStringLiteral("qpc"), qint64(b.qpc));
            o.insert(QStringLiteral("tick"), qint64(b.tick));
            QJsonArray calls, maxDelay;
            const char *srcKeys[3] = {"m", "k", "p"};
            QJsonObject callsO, maxDelayO;
            for (int i = 0; i < 3; ++i) {
                callsO.insert(QLatin1String(srcKeys[i]), qint64(b.calls[i]));
                maxDelayO.insert(QLatin1String(srcKeys[i]), qint64(b.maxDelay[i]));
            }
            o.insert(QStringLiteral("calls"), callsO);
            o.insert(QStringLiteral("maxDelay"), maxDelayO);
            o.insert(QStringLiteral("maxGap"), qint64(b.maxGap));
            o.insert(QStringLiteral("ring"), qint64(b.ringSize));
            o.insert(QStringLiteral("dropped"), qint64(b.dropped));
            o.insert(QStringLiteral("fgPid"), qint64(b.fgPid));
            appendLine(QJsonDocument(o).toJson(QJsonDocument::Compact));
        }

        for (const auto &inc : b.hookIncidents) {
            QJsonObject o;
            o.insert(QStringLiteral("k"), QStringLiteral("inc"));
            o.insert(QStringLiteral("type"), QStringLiteral("hook"));
            o.insert(QStringLiteral("qpc"), qint64(inc.qpc));
            o.insert(QStringLiteral("tick"), qint64(inc.tick));
            o.insert(QStringLiteral("src"), inc.src);
            o.insert(QStringLiteral("delayMs"), inc.delayMs);
            o.insert(QStringLiteral("durMs"), inc.durMs);
            o.insert(QStringLiteral("inj"), inc.injected ? 1 : 0);
            o.insert(QStringLiteral("fgPid"), qint64(inc.fgPid));
            o.insert(QStringLiteral("fgExe"), fgExe(inc.fgPid));
            appendLine(QJsonDocument(o).toJson(QJsonDocument::Compact));
        }
        for (const auto &inc : b.pumpIncidents) {
            QJsonObject o;
            o.insert(QStringLiteral("k"), QStringLiteral("inc"));
            o.insert(QStringLiteral("type"), QStringLiteral("pump"));
            o.insert(QStringLiteral("qpc"), qint64(inc.qpc));
            o.insert(QStringLiteral("tick"), qint64(inc.tick));
            o.insert(QStringLiteral("gapMs"), inc.gapMs);
            o.insert(QStringLiteral("fgPid"), qint64(inc.fgPid));
            o.insert(QStringLiteral("fgExe"), fgExe(inc.fgPid));
            appendLine(QJsonDocument(o).toJson(QJsonDocument::Compact));
        }

        if (!b.ctx.empty()) {
            // 上下文行：recs 为紧凑数组 [相对毫秒, tick, delayMs, durMs, src, inj, noEvent]，
            // 相对毫秒 = (记录 qpc − 异常 qpc)×1000/频率，负值即异常前
            QJsonArray recs;
            const int n = int(b.ctx.size());
            const int stride = n > kCtxMaxRecs ? (n + kCtxMaxRecs - 1) / kCtxMaxRecs : 1;
            for (int i = 0; i < n; i += stride) {
                const auto &r = b.ctx[size_t(i)];
                QJsonArray row;
                row.append(double(r.qpc - b.qpc) * 1000.0 / double(m_meta.qpcFreq));
                row.append(qint64(r.tick));
                row.append(r.delayMs);
                row.append(r.durMs);
                row.append(r.src);
                row.append(r.injected ? 1 : 0);
                row.append(r.noEvent ? 1 : 0);
                recs.append(row);
            }
            QJsonObject o;
            o.insert(QStringLiteral("k"), QStringLiteral("ctx"));
            o.insert(QStringLiteral("n"), n);
            o.insert(QStringLiteral("stride"), stride);
            o.insert(QStringLiteral("relTo"), QStringLiteral("stat.qpc"));
            o.insert(QStringLiteral("recs"), recs);
            appendLine(QJsonDocument(o).toJson(QJsonDocument::Compact));
        }

        f->write(out);
        f->flush(); // 证据文件：每批落盘，崩溃也能保住已有内容
    }

    MainThreadProbe *m_probe;
    Meta m_meta;
    QFile m_file;
    QDate m_fileDate;
    QHash<quint32, QString> m_fgExe;
};

MainThreadProbe *MainThreadProbe::instance()
{
    static MainThreadProbe *s_instance = nullptr;
    if (!s_instance)
        s_instance = new MainThreadProbe();
    return s_instance;
}

MainThreadProbe::MainThreadProbe()
    : QObject(QCoreApplication::instance())
{
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    m_qpcFreq = freq.QuadPart ? freq.QuadPart : 1;
    m_lastBeatQpc = [] {
        LARGE_INTEGER c{};
        QueryPerformanceCounter(&c);
        return c.QuadPart;
    }();

    // 落盘线程：AppLocalDataLocation/probe/（P-067 勘误后为
    // %LOCALAPPDATA%\PopTools\PopTools\probe\）
    ProbeWriterThread::Meta meta;
    meta.qpcFreq = m_qpcFreq;
    {
        LARGE_INTEGER c{};
        QueryPerformanceCounter(&c);
        meta.qpc0 = c.QuadPart;
    }
    meta.tick0 = GetTickCount();
    meta.epoch0 = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    meta.ver = QLatin1String(POPTOOLS_VERSION);
    {
        wchar_t path[MAX_PATH] = {};
        const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
        meta.exePath = QString::fromWCharArray(path, int(n));
    }
    meta.pid = GetCurrentProcessId();
    meta.dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
               + QStringLiteral("/probe");
    meta.host = QSysInfo::prettyProductName();

    auto *writer = new ProbeWriterThread(this, std::move(meta));
    m_writer = writer;
    writer->start();

    m_heartbeat = new QTimer(this);
    connect(m_heartbeat, &QTimer::timeout, this, &MainThreadProbe::onHeartbeat);
    m_heartbeat->start(kHeartbeatMs);
    m_flush = new QTimer(this);
    connect(m_flush, &QTimer::timeout, this, &MainThreadProbe::onFlushTick);
    m_flush->start(kFlushMs);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
            this, &MainThreadProbe::shutdown, Qt::DirectConnection);
}

MainThreadProbe::~MainThreadProbe()
{
    shutdown();
}

MainThreadProbe::HookCall::HookCall(Src src, quint32 eventTickMs, bool injected)
    : m_src(src)
{
    if (MainThreadProbe *p = instance())
        p->hookEnter(src, eventTickMs, injected);
}

MainThreadProbe::HookCall::~HookCall()
{
    if (MainThreadProbe *p = instance())
        p->hookExit(m_src);
}

void MainThreadProbe::hookEnter(Src src, quint32 eventTickMs, bool injected)
{
    Pending &p = m_pending[src];
    p.active = true;
    p.enterQpc = [] {
        LARGE_INTEGER c{};
        QueryPerformanceCounter(&c);
        return c.QuadPart;
    }();
    p.eventTick = eventTickMs;
    p.injected = injected;
}

void MainThreadProbe::hookExit(Src src)
{
    Pending &p = m_pending[src];
    if (!p.active)
        return; // 防御：无 enter 配对（不该发生）
    p.active = false;

    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    const qint64 nowQpc = c.QuadPart;
    const quint32 nowTick = GetTickCount();

    Rec r;
    r.qpc = nowQpc;
    r.tick = nowTick;
    // 事件戳为 0 视为无事件数据（nCode<0 调用），延迟不可知记 -1；
    // int32 差值天然容忍 GetTickCount 回绕（49.7 天）
    r.delayMs = p.eventTick ? qint32(nowTick - p.eventTick) : -1;
    const double dur = double(nowQpc - p.enterQpc) * 1000.0 / double(m_qpcFreq);
    r.durMs = qint16(dur < 32767.0 ? dur + 0.5 : 32767);
    r.src = quint8(src);
    r.injected = p.injected ? 1 : 0;
    r.noEvent = p.eventTick ? 0 : 1;

    m_ring.push_back(r);
    if (m_ring.size() > 200000) { // 200k 条 ≈ 数 MB，纯证据缓冲
        m_ring.pop_front();
        ++m_dropped;
    }
    ++m_calls[src];
    if (r.delayMs > qint32(m_maxDelay[src]))
        m_maxDelay[src] = quint32(r.delayMs > 0 ? r.delayMs : 0);

    // 异常合并：主线程一次卡顿会让排队事件成串放行（每条都是高延迟），
    // 同源 500ms 内只记一次异常，上下文窗口已覆盖整串
    if (r.delayMs >= kHookDelayReportMs
        && (m_lastIncidentQpc[src] == 0
            || nowQpc - m_lastIncidentQpc[src] > m_qpcFreq / 2)) {
        m_lastIncidentQpc[src] = nowQpc;
        HookIncident inc;
        inc.qpc = nowQpc;
        inc.tick = nowTick;
        inc.fgPid = captureForegroundPid();
        inc.delayMs = r.delayMs;
        inc.durMs = r.durMs;
        inc.src = quint8(src);
        inc.injected = p.injected;
        m_hookIncidents.push_back(inc);
    }
}

void MainThreadProbe::onHeartbeat()
{
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    const qint64 nowQpc = c.QuadPart;
    const double gapMs = double(nowQpc - m_lastBeatQpc) * 1000.0 / double(m_qpcFreq);
    m_lastBeatQpc = nowQpc;
    const auto gapI = qint32(gapMs + 0.5);
    if (gapI > qint32(m_maxGap))
        m_maxGap = quint32(gapI);
    if (gapI >= kPumpStallReportMs
        && (m_lastPumpQpc == 0 || nowQpc - m_lastPumpQpc > m_qpcFreq / 2)) {
        m_lastPumpQpc = nowQpc;
        PumpIncident inc;
        inc.qpc = nowQpc;
        inc.tick = GetTickCount();
        inc.fgPid = captureForegroundPid();
        inc.gapMs = gapI;
        m_pumpIncidents.push_back(inc);
    }
}

void MainThreadProbe::onFlushTick()
{
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    Batch b;
    b.qpc = c.QuadPart;
    b.tick = GetTickCount();
    b.fgPid = captureForegroundPid();
    for (int i = 0; i < 3; ++i) {
        b.calls[i] = m_calls[i];
        m_calls[i] = 0;
        b.maxDelay[i] = m_maxDelay[i];
        m_maxDelay[i] = 0;
    }
    b.maxGap = m_maxGap;
    m_maxGap = 0;
    b.ringSize = quint32(m_ring.size());
    b.dropped = m_dropped;
    b.hookIncidents = std::move(m_hookIncidents);
    m_hookIncidents.clear();
    b.pumpIncidents = std::move(m_pumpIncidents);
    m_pumpIncidents.clear();

    // 上下文窗口：本批全部异常取并集窗 [最早前2s, 最晚后1s]，从环形缓冲
    // 抽取（超限抽稀）。缓冲只在主线程写，此处同步读无竞争
    if (!b.hookIncidents.empty() || !b.pumpIncidents.empty()) {
        qint64 from = LLONG_MAX;
        qint64 to = LLONG_MIN;
        for (const auto &inc : b.hookIncidents) {
            from = qMin(from, inc.qpc - kCtxBeforeS * m_qpcFreq);
            to = qMax(to, inc.qpc + kCtxAfterS * m_qpcFreq);
        }
        for (const auto &inc : b.pumpIncidents) {
            from = qMin(from, inc.qpc - kCtxBeforeS * m_qpcFreq);
            to = qMax(to, inc.qpc + kCtxAfterS * m_qpcFreq);
        }
        b.ctx.reserve(std::min<size_t>(size_t(kCtxMaxRecs) * 2, m_ring.size()));
        const int n = int(m_ring.size());
        const int stride = n > kCtxMaxRecs ? (n + kCtxMaxRecs - 1) / kCtxMaxRecs : 1;
        for (int i = 0; i < n; i += stride) {
            const Rec &r = m_ring[size_t(i)];
            if (r.qpc >= from && r.qpc <= to)
                b.ctx.push_back(r);
        }
    }

    {
        QMutexLocker lock(&m_ioMutex);
        m_ioQueue.push_back(std::move(b));
        m_ioCond.wakeOne();
    }
}

void MainThreadProbe::shutdown()
{
    if (!m_heartbeat)
        return; // 未完成构造（理论不可达，防御）
    m_heartbeat->stop();
    m_flush->stop();
    onFlushTick(); // 冲账：退出前最后一批照常换出
    {
        QMutexLocker lock(&m_ioMutex);
        m_writerQuit = true;
        m_ioCond.wakeAll();
    }
    if (m_writer && m_writer->isRunning())
        m_writer->wait(3000);
    m_heartbeat = nullptr; // 幂等哨兵：aboutToQuit 与析构双路径只收一次
}

quint32 MainThreadProbe::captureForegroundPid() const
{
    const HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg)
        GetWindowThreadProcessId(fg, &pid);
    return pid;
}

} // namespace poptools
