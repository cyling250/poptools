#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class QNetworkAccessManager;

namespace poptools {

class DshRuntime;

// ============================================================
// DshUpdateService（T-077）：dsh 运行时自动更新。
//
// 查新：npm registry /@deepseek-ai/dsh/latest（官方渠道）对比本地
// node_modules/@deepseek-ai/dsh/package.json 的 version——package.json
// 是唯一可信口径（.installed-version 存在安装器/脚本双写的历史包袱，
// T-077 起不再依赖）。
//
// 更新编排（confirmUpdate）：DshRuntime::stop()（优雅停 sidecar，释放
// node_modules）→ stateChanged(Idle) 后 QProcess 异步
// npm.cmd install @deepseek-ai/dsh@<ver>（cwd=tools/dsh，代理环境注入）
// → 成功即 DshRuntime::start()（看门狗口径正常拉起，webUrlReady 信号
// 联动 WebView2 重新导航）；失败保留旧版本可继续用（start 兜底回拉）。
// ============================================================
class DshUpdateService : public QObject
{
    Q_OBJECT
public:
    explicit DshUpdateService(DshRuntime *runtime, QObject *parent = nullptr);

    // npm 路径/包目录在 GoodyModule 装配期注入（与 toolsDir() 同源）
    void configure(const QString &npmCmd, const QString &dshDir);
    void checkForUpdates(bool interactive);

    void confirmUpdate(const QString &version); // 用户确认后编排：停 sidecar → npm install → 重启

signals:
    void updateAvailable(const QString &localVersion, const QString &remoteVersion);
    void upToDate();
    void checkFailed(const QString &reason);
    void updateFinished(bool ok, const QString &detail); // 编排终态（UI 提示）

private:
    QString localVersion() const; // 读 package.json；解析失败返回空
    void runNpmInstall(const QString &version);

    DshRuntime *m_runtime = nullptr; // 非拥有（GoodyModule 父子）
    QNetworkAccessManager *m_nam = nullptr;
    QString m_npmCmd;
    QString m_dshDir;
    QString m_pendingVersion;    // 已确认待更新版本（停净 sidecar 后消费）
    QString m_installingVersion; // npm 在途版本（finished 回报用）
    QProcess m_npm;
};

} // namespace poptools
