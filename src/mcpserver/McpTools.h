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
//   goody/daily/<日期>.md、goody/monthly/<yyyy-MM>.md（日历摘要链路约定路径）
// ============================================================

// MCP 工具执行结果：payload 为工具的结构化输出；isError 时协议层给
// content 标记 isError=true
struct McpToolResult
{
    QJsonObject payload;
    bool isError = false;
};

// ---- 工具描述（tools/list 载荷；与 run* 一一对应） ----
QJsonArray mcpToolDescriptors();

// ---- 工具实现 ----
// 活动数据读取（原 monitor.query 逻辑移植，流白名单防路径穿越）
McpToolResult runActivityDaily(const QString &monitorBaseDir, const QJsonObject &args);
// 日报写入（dsh 定时任务成文落 goody 约定路径 → 日历摘要链路续命）
McpToolResult runWriteDailyDigest(const QString &goodyBaseDir, const QJsonObject &args);
McpToolResult runReadDailyDigest(const QString &goodyBaseDir, const QJsonObject &args);
McpToolResult runWriteMonthlyDigest(const QString &goodyBaseDir, const QJsonObject &args);
McpToolResult runReadMonthlyDigest(const QString &goodyBaseDir, const QJsonObject &args);
// 截图（整虚拟桌面拼图，PNG 落临时目录，返回路径供 dsh 读图/OCR）
McpToolResult runCaptureScreenshot(const QJsonObject &args);

} // namespace poptools
