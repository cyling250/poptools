#pragma once

#include <QList>

#include "ToolRegistry.h"

namespace poptools {

// fs.* 工具组：list / read / write / move / delete（删除走回收站）。
// 全部为小体积、用户触发、有上限的本地文件操作（KB 级，毫秒级完成），
// 同步回调即可——真正的"重 IO"（日志/OCR/命令）在各自的 worker/子进程。
// 权限：list/read 只读放行；write/move/delete 走确认。
QList<ToolDef> makeFsTools();

} // namespace poptools
