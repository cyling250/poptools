#pragma once

// ============================================================
// 桌面面板跨进程协议常量 —— 应用侧（src/ui/shared/DesktopSharedCanvas /
// DesktopSurfaceHost）与 explorer 注入 DLL（src/hook/calendar_hook.cpp）
// 共用的唯一事实源（T-062 S-01：原先三处手抄，改这里两侧同步生效）。
//
// 纯常量头：不含 Qt/STL、不强制依赖 windows.h（WM_APP 缺失时自带兜底）——
// DLL 编译目标是纯 Win32 + 静态 CRT，绝不能因此引入 Qt 链接。
//
// ⚠ 改任何值都是跨进程破坏性变更：必须两端同时更新；共享头 120 字节布局
//（LONG×5 + slotRect[SLOTS][4] + seq）与段名/魔数是行为红线（失配即拒画），
// calendar_hook.cpp 里有 static_assert 逐字段核对，布局漂移编译期即报错。
// ============================================================

#ifndef WM_APP
#define WM_APP 0x8000
#endif

// ---- 共享内存段：CreateFileMapping(命名) + CreateDIBSection 绑定 OFFSET 处 ----
#define POPTOOLS_DESKTOP_SHM_NAME   L"Local\\PopToolsDesktopShared_v3"
#define POPTOOLS_DESKTOP_SHM_SLOTS  6         // 面板槽位数（v3：任意贴壁纸面板占一槽）
#define POPTOOLS_DESKTOP_SHM_OFFSET 4096      // 头部保留区大小 = 位图数据起点
#define POPTOOLS_DESKTOP_SHM_MAGIC  0x434C4B33 // 'CLK3'（v3 槽位数组，结构不兼容失配拒画）

// ---- 共享头字段偏移（字节，字段均为 LONG；与 DLL 侧 SharedHeader pack(1)
//      布局逐字节对齐，calendar_hook.cpp 有 static_assert 核对）----
#define POPTOOLS_HDR_MAGIC     0
#define POPTOOLS_HDR_WIDTH     4
#define POPTOOLS_HDR_HEIGHT    8
#define POPTOOLS_HDR_READY     12
#define POPTOOLS_HDR_HOSTWND   16
#define POPTOOLS_HDR_SLOTRECT0 20 // slotRect[SLOTS][4]，每槽 16 字节
#define POPTOOLS_HDR_SEQ       (POPTOOLS_HDR_SLOTRECT0 + POPTOOLS_DESKTOP_SHM_SLOTS * 16)

// ---- 自定义消息（应用 1x1 宿主窗 ↔ DLL 消息窗）----
#define POPTOOLS_MSG_CLONE_UPDATE (WM_APP + 0x101u) // 应用→DLL：按槽矩形失效重绘
#define POPTOOLS_MSG_CLONE_STOP   (WM_APP + 0x102u) // 应用→DLL：清理注入并自毁（DLL 内部消费）
#define POPTOOLS_MSG_CLONE_EVENT  (WM_APP + 0x103u) // DLL→应用：鼠标事件转发

// ---- 鼠标事件号（wParam 低 16 位；高 16 位 extra：1=点在桌面图标上，宿主穿透）----
#define POPTOOLS_EV_MOVE   1
#define POPTOOLS_EV_DOWN   2
#define POPTOOLS_EV_UP     3
#define POPTOOLS_EV_DBLCLK 4
#define POPTOOLS_EV_LEAVE  5
