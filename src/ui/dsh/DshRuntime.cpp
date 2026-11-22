#include "DshRuntime.h"

#include <QDateTime>
#include <QDir>
#include <QFile>

#include <algorithm>
#include <vector>

#ifdef Q_OS_WIN
#include <winsock2.h> // AF_INET/u_short（dsh 库全局 WIN32_LEAN_AND_MEAN 排除 winsock）
#include <windows.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#endif

namespace poptools {

namespace {
// dsh web-app 启动行前缀（web-app/src/index.ts：console.log(`${label}: ${url}`)，
// label 恒为 'dsh web'）
constexpr QLatin1String kUrlLinePrefix("dsh web: ");
constexpr int kKillGraceMs = 3000;
// 看门狗退避：2s → 6s → 18s → 54s → 封顶 120s（防 crash-loop 疯狂重启）
constexpr int kRestartBaseMs = 2000;
constexpr int kRestartCapMs = 120000;
constexpr int kStderrTailLines = 20;
} // namespace

DshRuntime::DshRuntime(QObject *parent)
    : QObject(parent)
{
    m_killTimer.setSingleShot(true);
    m_killTimer.setInterval(kKillGraceMs);
    connect(&m_killTimer, &QTimer::timeout, this, [this] {
        if (m_state == State::Stopping && m_process.state() == QProcess::Running)
            m_process.kill(); // 宽限期过仍存活 → 强杀（finished 收尾）
    });

    m_restartTimer.setSingleShot(true);
    connect(&m_restartTimer, &QTimer::timeout, this, [this] {
        if (m_state != State::Stopping && m_process.state() != QProcess::Running)
            start(); // 看门狗到点拉起（stop() 已把 timer 停掉，此路径恒非主动关停）
    });

    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        const QStringList lines = QString::fromUtf8(m_process.readAllStandardOutput())
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines)
            handleLine(line.trimmed());
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        const QStringList lines = QString::fromUtf8(m_process.readAllStandardError())
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            emit outputLine(line.trimmed()); // 诊断通道（Node 告警等）透传
            m_stderrTail.append(line.trimmed()); // 看门狗日志用尾部环形缓冲
            if (m_stderrTail.size() > kStderrTailLines)
                m_stderrTail.removeFirst();
        }
    });
    connect(&m_process, &QProcess::finished, this, &DshRuntime::handleFinished);
    connect(&m_process, &QProcess::errorOccurred, this, &DshRuntime::handleError);
}

void DshRuntime::configure(const QString &nodeExe, const QString &dshBinJs, const QString &dshHome,
                           const QString &agentsHome, const QString &apiKey)
{
    m_nodeExe = nodeExe;
    m_dshBinJs = dshBinJs;
    m_dshHome = dshHome;
    m_agentsHome = agentsHome;
    m_apiKey = apiKey;
    // 运行时日志放数据根（dshHome = <dataRoot>/dsh-home → 上级即 dataRoot）
    m_logPath = QDir::cleanPath(m_dshHome + QStringLiteral("/../dsh-runtime.log"));
}

void DshRuntime::start()
{
    if (m_process.state() == QProcess::Running)
        return;
    if (m_nodeExe.isEmpty() || m_dshBinJs.isEmpty()) {
        emit failed(QStringLiteral("DshRuntime 未配置（node/dsh bin 路径为空）"));
        setState(State::Failed);
        return;
    }
    killOrphanOnWebPort(); // 孤儿 sidecar 清理（T-077c：强杀宿主留下的 node
                           // 继续占 3080，新实例 EADDRINUSE 反复失败）
    m_webUrl.clear();
    setState(State::Starting);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // 家目录与共享 agent 配置根收进 PopTools 数据区（不污染 ~/.dsh ~/.agents）
    env.insert(QStringLiteral("DSH_HOME"), m_dshHome);
    env.insert(QStringLiteral("DSH_AGENTS_HOME"), m_agentsHome);
    // DeepSeek API Key：dsh 官方 sidecar 供 Key 路径（llm-deepseek-api-key 的
    // MISSING_CREDENTIAL 提示明示可经启动环境导出 DEEPSEEK_API_KEY）；key 来源
    // 本机 PopTools.ini [dsh]，不入仓库
    if (!m_apiKey.isEmpty())
        env.insert(QStringLiteral("DEEPSEEK_API_KEY"), m_apiKey);
    m_process.setProcessEnvironment(env);
    m_process.start(m_nodeExe, { m_dshBinJs, QStringLiteral("web"), QStringLiteral("--no-open") });
}

// 孤儿 sidecar 清理（T-077c）：宿主被强杀（taskkill /F 无清理机会）时 dsh
// sidecar 成孤儿继续监听 3080，新 PopTools 实例的 sidecar 每次 EADDRINUSE
// 失败、看门狗反复空转（dsh-runtime.log 实证 attempts=10）。启动前查 3080
// 监听 PID，映像路径是本运行时的 node（tools/node/current）才终止——不误伤
// 其他进程恰好占 3080 的情况（让 dsh 自己的 EADDRINUSE 报错兜底暴露）。
void DshRuntime::killOrphanOnWebPort()
{
#ifdef Q_OS_WIN
    constexpr u_short kWebPort = 3080;
    DWORD size = 0;
    if (GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET,
                            TCP_TABLE_OWNER_PID_LISTENER, 0) != ERROR_INSUFFICIENT_BUFFER)
        return;
    std::vector<char> buf(size);
    auto *table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID *>(buf.data());
    if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET,
                            TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR)
        return;
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_TCPROW_OWNER_PID &row = table->table[i];
        if (ntohs(static_cast<u_short>(row.dwLocalPort)) != kWebPort)
            continue;
        const DWORD pid = row.dwOwningPid;
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                                  FALSE, pid);
        if (!proc)
            continue;
        WCHAR imagePath[MAX_PATH] = {};
        DWORD len = MAX_PATH;
        if (QueryFullProcessImageNameW(proc, 0, imagePath, &len)) {
            const QString image = QString::fromWCharArray(imagePath);
            if (image.contains(QStringLiteral("tools\\node\\current\\node.exe"),
                               Qt::CaseInsensitive)) {
                appendRuntimeLog(QStringLiteral("清理孤儿 sidecar（占 3080，pid %1）")
                                     .arg(pid));
                TerminateProcess(proc, 1);
                WaitForSingleObject(proc, 3000); // 端口释放需进程真正退出
            }
        }
        CloseHandle(proc);
    }
#endif
}

void DshRuntime::stop()
{
    m_restartTimer.stop(); // 主动关停：取消任何待发的看门狗重启
    if (m_process.state() != QProcess::Running) {
        setState(State::Idle);
        return;
    }
    setState(State::Stopping);
    m_process.terminate(); // node 控制台程序多半不理会 WM_CLOSE → 宽限期后 kill
    m_killTimer.start();
}

void DshRuntime::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void DshRuntime::handleLine(const QString &line)
{
    emit outputLine(line);
    if (line.startsWith(kUrlLinePrefix) && m_webUrl.isEmpty()) {
        m_webUrl = line.mid(kUrlLinePrefix.size()).trimmed();
        setState(State::Ready);
        m_restartAttempts = 0; // 看门狗：成功就绪即重置退避
        emit webUrlReady(m_webUrl);
    }
}

void DshRuntime::handleFinished(int exitCode, QProcess::ExitStatus status)
{
    m_killTimer.stop();
    if (m_state == State::Stopping) {
        setState(State::Idle);
        return;
    }
    // 看门狗（T-076b）：非 stop 请求下的进程退出（崩溃/自行退出——sidecar
    // 不应自行退出）→ 落盘诊断 + 自动重启（指数退避）
    const QString reason = status == QProcess::CrashExit
                               ? QStringLiteral("dsh 进程崩溃退出")
                               : QStringLiteral("dsh 进程自行退出（code %1）").arg(exitCode);
    appendRuntimeLog(reason);
    setState(State::Failed);
    emit failed(reason);
    scheduleRestart();
}

void DshRuntime::handleError(QProcess::ProcessError error)
{
    // FailedToStart 最关键：node 路径错/文件缺失。Crashed 由 finished 收尾，
    // 此处只报 Starting 期错误避免双重 failed
    if (error == QProcess::FailedToStart) {
        appendRuntimeLog(QStringLiteral("dsh 进程启动失败：node 或 bin.js 路径无效"));
        setState(State::Failed);
        emit failed(QStringLiteral("dsh 进程启动失败：node 或 bin.js 路径无效"));
        scheduleRestart(); // 退避节流下持续重试（最长 120s 一次）
    }
}

void DshRuntime::scheduleRestart()
{
    const int delay = std::min(kRestartCapMs,
                               kRestartBaseMs * (1 << std::min(m_restartAttempts, 6)));
    ++m_restartAttempts;
    m_restartTimer.start(delay);
}

void DshRuntime::appendRuntimeLog(const QString &reason)
{
    QFile log(m_logPath);
    if (!log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return; // 日志失败不阻断看门狗（重启优先）
    log.write(QDateTime::currentDateTime()
                  .toString(QStringLiteral("[yyyy-MM-dd hh:mm:ss] "))
                  .toUtf8());
    log.write(reason.toUtf8());
    log.write(QStringLiteral("  attempts=%1\n").arg(m_restartAttempts).toUtf8());
    for (const QString &line : m_stderrTail) {
        log.write(QStringLiteral("  | %1\n").arg(line).toUtf8());
    }
    // 简单限幅：日志超 256KB 换代（删除重建，历史丢失可接受——看门狗日志
    // 只为最近诊断）
    if (log.size() > 256 * 1024) {
        log.close();
        QFile::remove(m_logPath);
    }
}

} // namespace poptools
