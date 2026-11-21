#include "DshRuntime.h"

namespace poptools {

namespace {
// dsh web-app 启动行前缀（web-app/src/index.ts：console.log(`${label}: ${url}`)，
// label 恒为 'dsh web'）
constexpr QLatin1String kUrlLinePrefix("dsh web: ");
constexpr int kKillGraceMs = 3000;
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

    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        const QStringList lines = QString::fromUtf8(m_process.readAllStandardOutput())
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines)
            handleLine(line.trimmed());
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        const QStringList lines = QString::fromUtf8(m_process.readAllStandardError())
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines)
            emit outputLine(line.trimmed()); // 诊断通道（Node 告警等）透传
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

void DshRuntime::stop()
{
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
        emit webUrlReady(m_webUrl);
    }
}

void DshRuntime::handleFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(exitCode);
    m_killTimer.stop();
    if (m_state == State::Stopping) {
        setState(State::Idle);
        return;
    }
    // 非 stop 请求下的进程退出：启动期/就绪期崩溃（CrashReportExit 正常退出
    // 也按失败对待——sidecar 不应自行退出）
    setState(State::Failed);
    emit failed(status == QProcess::CrashExit ? QStringLiteral("dsh 进程崩溃退出")
                                              : QStringLiteral("dsh 进程自行退出（code %1）").arg(exitCode));
}

void DshRuntime::handleError(QProcess::ProcessError error)
{
    // FailedToStart 最关键：node 路径错/文件缺失。Crashed 由 finished 收尾，
    // 此处只报 Starting 期错误避免双重 failed
    if (error == QProcess::FailedToStart) {
        setState(State::Failed);
        emit failed(QStringLiteral("dsh 进程启动失败：node 或 bin.js 路径无效"));
    }
}

} // namespace poptools
