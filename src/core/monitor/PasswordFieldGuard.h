#pragma once

namespace poptools {

// ============================================================
// 密码框检测：判定"前台焦点控件是否为密码输入框"。
//
// 三层启发（由轻到重，均为主线程调用、每键事件一次，整体 <10µs）：
//   1. GetGUIThreadInfo 取前台线程焦点控件；
//   2. 窗口样式 ES_PASSWORD（Win32 Edit 的密码标志）；
//   3. 类名/进程名含 "password"（兜底自绘控件的自报家门）。
//
// 结果按 hwnd 缓存 500ms：同控件连打时不重复探测。
// 已知边界：浏览器/UWP 的自绘密码框若无 ES_PASSWORD、类名亦无特征
// 则漏判（v1 接受；UIA IsPassword 增强留给快照管线阶段一并评估）。
// ============================================================
namespace PasswordFieldGuard {

// 焦点是否在密码框（主线程调用；非主线程结果未定义）
bool isPasswordFocused();

} // namespace PasswordFieldGuard

} // namespace poptools
