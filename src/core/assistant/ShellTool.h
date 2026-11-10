#pragma once

#include "ToolRegistry.h"

namespace poptools {

// shell.run 工具：PowerShell 执行命令。
// QProcess 事件循环异步（无阻塞、无等待），超时看门狗杀进程，
// 输出强制 UTF-8 解码、截断防刷屏。工具的"敏感"属性由
// PermissionManager 按名称判定（此处 readOnly=false）。
ToolDef makeShellRunTool();

} // namespace poptools
