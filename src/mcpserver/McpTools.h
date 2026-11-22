#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QString>

namespace poptools {

// ============================================================
// McpTools：PopTools MCP 服务器的工具实现（T-074 M2 能力桥）。
//
// 每个工具 = 纯函数（参数 JSON → 结果 JSON），协议层（main.cpp）负责
// MCP 框架（initialize/tools list/call、content 包装）。文件工具同步
// 直读（单文件 MB 级以内，无需线程池）；OCR 为异步服务调用，由协议层
// 特化处理（本文件只留声明式的参数/schema）。
//
// 目录约定（与主程序 QStandardPaths::AppLocalDataLocation 同口径：
// %LOCALAPPDATA%/PopTools/PopTools）：
//   monitor/<日期>/{timeline,keys,textlog,ocrtext,clipboard}.jsonl
//   dsh/daily/<日期>.md、dsh/monthly/<yyyy-MM>.md（日历摘要链路约定路径）
// ============================================================

// MCP 工具执行结果：payload 为工具的结构化输出；isError 时协议层给
// content 标记 isError=true
struct McpToolResult
{
    QJsonObject payload;
    bool isError = false;
    // 显式双参构造：调用点的 *out = {payload, isError} 拷贝列表赋值依赖它
    //（聚合体在 MSVC 上的列表赋值口径不稳，C2679 实测）
    McpToolResult() = default;
    McpToolResult(QJsonObject p, bool e)
        : payload(p), isError(e)
    {
    }
};

// ---- 工具描述（tools/list 载荷；与 run* 一一对应） ----
QJsonArray mcpToolDescriptors();

// ---- 工具实现 ----
// 活动数据读取（原 monitor.query 逻辑移植，流白名单防路径穿越）
McpToolResult runActivityDaily(const QString &monitorBaseDir, const QJsonObject &args);
// 日报写入（dsh 定时任务成文落 dsh 约定路径 → 日历摘要链路续命）
McpToolResult runWriteDailyDigest(const QString &dshBaseDir, const QJsonObject &args);
McpToolResult runReadDailyDigest(const QString &dshBaseDir, const QJsonObject &args);
McpToolResult runWriteMonthlyDigest(const QString &dshBaseDir, const QJsonObject &args);
McpToolResult runReadMonthlyDigest(const QString &dshBaseDir, const QJsonObject &args);
// 截图（整虚拟桌面拼图，PNG 落临时目录，返回路径供 dsh 读图/OCR）
McpToolResult runCaptureScreenshot(const QJsonObject &args);

// ---- 域工具扩展（T-074 M2.5「全功能交底」）：日历/活动设置/收纳盒/翻译/
// 系统/上下文管理。runDomainTool 未命中时返回 false（协议层回落核心工具），
// domainToolDescriptors 追加进 tools/list ----
QJsonArray domainToolDescriptors();
bool runDomainTool(const QString &monitorBaseDir, const QString &dataRoot,
                   const QString &name, const QJsonObject &args, McpToolResult *out);
// 心跳触碰：每次 tools/call 由协议层调（写 dataRoot/mcp.heartbeat，失败忽略）
void touchMcpHeartbeat(const QString &dataRoot);

} // namespace poptools
