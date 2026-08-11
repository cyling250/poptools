#pragma once

#include <QRect>
#include <QPoint>

namespace snipaste {

// ============================================================
// 智能框选：检测鼠标逻辑坐标下方"最合适"的区域（全局逻辑坐标）
//
//   * detectUiElementRect：MSAA 控件级检测（Snipaste 同款）——
//     先枚举鼠标下的顶层窗口（排除截图画布自身），再对该窗口客户区
//     accHitTest 取鼠标下 UI 元素（按钮/输入框/面板等）的精确矩形。
//   * detectWindowRect：回退方案——取包含鼠标点的最小面积顶层窗口矩形。
//
// 非 Windows 平台返回空矩形。
// ============================================================

// MSAA 控件级检测：返回鼠标下 UI 元素的精确矩形（逻辑坐标）；无合适元素返回空 QRect
QRect detectUiElementRect(const QPoint &logicalPos, void *excludeHwnd, qreal dpr);

// 窗口级回退：返回包含鼠标点的最小面积顶层窗口矩形（逻辑坐标）
QRect detectWindowRect(const QPoint &logicalPos, void *excludeHwnd, qreal dpr);

} // namespace snipaste
