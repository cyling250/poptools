#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

namespace poptools {

// ============================================================
// DshRuntime：dsh sidecar 运行时管理（T-074 M1）。
//
// QProcess 托管 `node bin.js web --no-open`（dsh webserver profile，
// 端口默认 3080，--no-open 防抢开默认浏览器）；stdout 解析启动行
// 「dsh web: <url>」——URL 含进程级 ?token= 登录态（dsh 每次启动随机
// 生成，访问后种 30 天 HttpOnly cookie），即 WebView2 的导航入口。
//
// 家目录隔离：DSH_HOME / DSH_AGENTS_HOME 指向 PopTools 数据区
//（默认 ~/.dsh、~/.agents 不污染）；API Key 等凭据由 dsh
// credentials-local 在 home 内自管，PopTools 不读不存。
// ============================================================
class DshRuntime : public QObject
{
    Q_OBJECT
public:
    enum class State
    {
        Idle,     // 未启动
        Starting, // 进程已拉起，等待启动 URL
        Ready,    // 已解析出 web URL（可交给 WebView2）
        Stopping, // 优雅关停中（宽限期后强杀）
        Failed,   // 启动失败或崩溃（PoC 阶段不自动重启，M3 细化策略）
    };
    Q_ENUM(State)

    explicit DshRuntime(QObject *parent = nullptr);

    void configure(const QString &nodeExe, const QString &dshBinJs, const QString &dshHome,
                   const QString &agentsHome, const QString &apiKey = QString());
    void start();
    void stop();

    State state() const { return m_state; }
    QString webUrl() const { return m_webUrl; }

signals:
    void stateChanged(poptools::DshRuntime::State state);
    void webUrlReady(const QString &url); // 解析出带 token 的启动 URL
    void outputLine(const QString &line); // stdout/stderr 原始行（PoC 日志窗用）
    void failed(const QString &reason);   // 崩溃/启动失败

private:
    void setState(State state);
    void handleLine(const QString &line);
    void handleFinished(int exitCode, QProcess::ExitStatus status);
    void handleError(QProcess::ProcessError error);

    QProcess m_process;
    QTimer m_killTimer; // stop() 宽限期计时（terminate 无效则 kill）
    QString m_nodeExe;
    QString m_dshBinJs;
    QString m_dshHome;
    QString m_agentsHome;
    QString m_apiKey;   // DEEPSEEK_API_KEY 经启动环境注入（dsh 官方 sidecar 供 Key 路径）
    QString m_webUrl;
    State m_state = State::Idle;
};

} // namespace poptools
