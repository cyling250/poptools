#pragma once

#include <QtGlobal>

namespace poptools {

// ============================================================
// PopTools 自身剪贴板写入排除（T-047-CLIP）。
//
// 本进程任何写剪贴板的代码在 set 调用返回后立即调 noteSelfWrite()，
// 记下 GetClipboardSequenceNumber()；剪贴板监听器（ClipboardListener）
// 对观测到的更新 seq 查 isSelfWrite(seq)，命中即整条跳过——监控流里
// 不出现"PopTools 自己复制了什么"。
//
// 时序说明：WM_CLIPBOARDUPDATE 在 SetClipboardData 期间同步派发给
// 同线程的监听窗口，此刻 seq 已自增完毕；noteSelfWrite 随后紧贴写调用
// 结束执行，两边读到的都是同一个 seq，防抖（300ms，T-047-FIX(P3-3)）
// 结束后判定必然成立。
//
// 契约：新增写剪贴板的代码必须跟随调用 noteSelfWrite()
//（现有埋点 grep "noteSelfWrite"）；漏埋只会导致该次写入被误记一条
// 日志（无害方向），不会影响其它功能。
// ============================================================
namespace OwnWriteGuard {

// 自身写入完成后立即调用（任意线程；读 GetClipboardSequenceNumber）
void noteSelfWrite();

// 观测到的更新 seq 是否来自自身写入（0 恒为 false）
bool isSelfWrite(qint64 seq);

// 测试隔离：清空记录
void reset();

} // namespace OwnWriteGuard

} // namespace poptools
