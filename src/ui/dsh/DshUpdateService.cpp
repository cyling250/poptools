#include "DshUpdateService.h"

#include "DshRuntime.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUrl>

namespace poptools {

namespace {
constexpr char kNpmLatestUrl[] =
    "https://registry.npmjs.org/@deepseek-ai/dsh/latest";
constexpr int kCheckTimeoutMs = 15000;
constexpr int kNpmTimeoutMs = 15 * 60 * 1000; // 1.3G 级依赖树，15 分钟上限
constexpr int kKillGraceMs = 5000;            // 停 sidecar 后等锁释放
} // namespace

DshUpdateService::DshUpdateService(DshRuntime *runtime, QObject *parent)
    : QObject(parent)
    , m_runtime(runtime)
{
    // 停净 sidecar（Idle）后再跑 npm——node_modules 被 node 进程锁着换不动
    connect(m_runtime, &DshRuntime::stateChanged, this,
            [this](DshRuntime::State state) {
                if (state == DshRuntime::State::Idle && !m_pendingVersion.isEmpty())
                    runNpmInstall(m_pendingVersion);
            });
    connect(&m_npm, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        const QString version = m_installingVersion;
        m_installingVersion.clear();
        if (st == QProcess::NormalExit && code == 0) {
            m_runtime->start(); // 重启 sidecar（Ready 后 WebView2 自动重导航）
            emit updateFinished(true,
                                QStringLiteral("dsh 已更新到 %1").arg(version));
        } else {
            m_runtime->start(); // 失败兜底回拉旧版本（管家可用性优先）
            emit updateFinished(false,
                                QStringLiteral("npm 更新失败（code %1），已回拉旧版本")
                                    .arg(code));
        }
    });
}

void DshUpdateService::configure(const QString &npmCmd, const QString &dshDir)
{
    m_npmCmd = npmCmd;
    m_dshDir = dshDir;
}

QString DshUpdateService::localVersion() const
{
    QFile f(m_dshDir + QStringLiteral("/node_modules/@deepseek-ai/dsh/package.json"));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QJsonDocument::fromJson(f.readAll())
        .object()
        .value(QLatin1String("version"))
        .toString();
}

void DshUpdateService::checkForUpdates(bool interactive)
{
    if (m_npmCmd.isEmpty() || m_dshDir.isEmpty()) {
        if (interactive)
            emit checkFailed(QStringLiteral("dsh 运行时未就绪"));
        return;
    }
    const QString local = localVersion();
    if (local.isEmpty()) {
        if (interactive)
            emit checkFailed(QStringLiteral("本地 dsh 版本不可读（未安装运行时）"));
        return;
    }
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(QLatin1String(kNpmLatestUrl))};
    req.setTransferTimeout(kCheckTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, interactive, local] {
        reply->deleteLater();
        const QString reason =
            QStringLiteral("无法获取 dsh npm 版本信息（请检查网络连接）");
        if (reply->error() != QNetworkReply::NoError) {
            if (interactive)
                emit checkFailed(reason);
            return;
        }
        const QString remote = QJsonDocument::fromJson(reply->readAll())
                                   .object()
                                   .value(QLatin1String("version"))
                                   .toString();
        if (remote.isEmpty()) {
            if (interactive)
                emit checkFailed(reason);
            return;
        }
        if (remote == local) {
            if (interactive)
                emit upToDate();
            return;
        }
        emit updateAvailable(local, remote);
    });
}

void DshUpdateService::confirmUpdate(const QString &version)
{
    if (version.isEmpty() || m_runtime->state() == DshRuntime::State::Starting)
        return;
    m_pendingVersion = version;
    // 编排第一步：停 sidecar（Idle 信号触发 runNpmInstall）
    m_runtime->stop();
    // 兜底：sidecar 若已不在跑（Idle/Failed），stateChanged 可能不来——
    // 延时轮询一次确认（幂等：pendingVersion 在 npm 启动时清空）
    QTimer::singleShot(kKillGraceMs, this, [this] {
        if (!m_pendingVersion.isEmpty()
            && m_runtime->state() == DshRuntime::State::Idle)
            runNpmInstall(m_pendingVersion);
    });
}

void DshUpdateService::runNpmInstall(const QString &version)
{
    if (m_npm.state() == QProcess::Running || version.isEmpty())
        return;
    m_installingVersion = version;
    m_pendingVersion.clear(); // 防止 Idle 信号重复触发
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // npm registry（国外源）代理兜底：显式环境缺失时落本机代理（AGENTS.md 出网纪律）
    if (env.value(QStringLiteral("HTTPS_PROXY")).isEmpty()) {
        env.insert(QStringLiteral("HTTPS_PROXY"), QStringLiteral("http://127.0.0.1:17890"));
        env.insert(QStringLiteral("HTTP_PROXY"), QStringLiteral("http://127.0.0.1:17890"));
    }
    m_npm.setProcessEnvironment(env);
    m_npm.setWorkingDirectory(m_dshDir);
    m_npm.start(m_npmCmd, { QStringLiteral("install"),
                            QStringLiteral("@deepseek-ai/dsh@") + version });
    QTimer::singleShot(kNpmTimeoutMs, &m_npm, [this] {
        if (m_npm.state() == QProcess::Running)
            m_npm.kill(); // 超时强杀 → finished 非零退出走兜底回拉
    });
}

} // namespace poptools
