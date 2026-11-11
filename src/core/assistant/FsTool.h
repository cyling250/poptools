#pragma once

#include <QList>

#include "ToolRegistry.h"

namespace poptools {

// fs.* 工具组：list / read / write / move / delete（删除走回收站）。
// 全部为小体积、用户触发、有上限的本地文件操作（KB 级，毫秒级完成），
// 同步回调即可——真正的"重 IO"（日志/OCR/命令/目录枚举）在各自的
// worker/子进程/线程池（R6-C1：fs.list 目录枚举挪线程池，巨目录不再
// 卡 GUI 线程）。权限：list/read 只读放行；write/move/delete 走确认。
// ctx：异步结果回主线程交付的锚点对象（MonitorQueryTool 同款，
// QPointer 守护：锚点为空/销毁后在途结果整体丢弃）。R6-C1 后 fs.list
// 走线程池异步交付，null 锚点意味着其结果整体丢弃——不再设默认值，
// 调用方必须显式表态（生产装配传实锚点；只测同步工具的单测传 nullptr）
QList<ToolDef> makeFsTools(QObject *ctx);

} // namespace poptools
