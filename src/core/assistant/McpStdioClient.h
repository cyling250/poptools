#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <functional>

class QProcess;

namespace poptools {

// ============================================================
// MCP（Model Context Protocol）stdio 客户端：把外部工具服务器桥接进
// goody 的 ToolRegistry（工具名 mcp__<server>__<tool>）。
//
// 协议：JSON-RPC 2.0，换行分隔（stdio transport）。握手 initialize →
// notifications/initialized → ready；请求按 id 匹配应答，30s 看门狗。
//
// 配置 goody/mcp.json（用户手编，MCP 为可选能力）：
//   {"servers":[{"name":"fs","command":"npx","args":["-y","@modelcontextprotocol/server-filesystem","D:/"]}]}
// ============================================================
class McpStdioClient : public QObject
{
    Q_OBJECT
public:
    struct ServerConfig
    {
        QString name;
        QString command;
        QStringList args;
    };

    // 读取 mcp.json；文件缺失/格式错误 → 空表（MCP 是可选能力）
    static QList<ServerConfig> loadConfig(const QString &goodyDir);

    explicit McpStdioClient(const ServerConfig &cfg, QObject *parent = nullptr);
    ~McpStdioClient() override;

    void start(); // 拉起子进程 + initialize 握手（幂等：进程在手即返回）
    void stop();  // 停止：kill 子进程 + 在途请求统一按"MCP 已停止"失败回调

    bool ready() const { return m_ready; }
    QString serverName() const { return m_cfg.name; }

    // 工具清单：[{name, description?, inputSchema}]（异步）
    void listTools(const std::function<void(bool ok, QJsonArray tools, QString err)> &done);

    // 调用工具：结果 content 数组（[{type:"text",text:…}]）原样回传
    void callTool(const QString &tool, const QJsonObject &arguments,
                  const std::function<void(bool ok, QJsonArray content, QString err)> &done);

signals:
    void readyChanged();

private:
    void send(const QJsonObject &msg);
    void onStdoutLine(const QByteArray &line);
    void failPending(const QString &err);

    ServerConfig m_cfg;
    QProcess *m_proc = nullptr;
    QByteArray m_buf; // 半行残留
    int m_nextId = 1;
    QHash<int, std::function<void(const QJsonObject &)>> m_pending;
    bool m_ready = false;
    // 本次连接 initialize 握手是否完成：区分"握手未完成失败"（errorOccurred/
    // finished 时补 stop 使 start 可重入，T-062 R2-F1）与"ready 后进程死亡"
    // （维持基线不自动重启）；errorOccurred 分支会先复位 m_ready，故须独立标志
    bool m_handshakeDone = false;
};

} // namespace poptools
