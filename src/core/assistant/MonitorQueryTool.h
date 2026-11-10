#pragma once

#include <QObject>
#include <QString>

#include "ToolRegistry.h"

namespace poptools {

// monitor.query 工具：读取活动监控日志（timeline/keys/ocrtext）供 goody
// 回答"我今天干了什么"。文件读取在线程池（红线1），结果经队列回主线程。
// ctx 为调用方生命周期锚点（GoodyModule），模块析构后结果静默丢弃。
ToolDef makeMonitorQueryTool(const QString &monitorBaseDir, QObject *ctx);

} // namespace poptools
