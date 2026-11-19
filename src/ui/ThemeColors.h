#pragma once

#include <QColor>
#include <QGuiApplication>
#include <QStyleHints>
#include <QtGlobal>

namespace poptools {

// ============================================================
// 截图覆盖层绘制语义色（paintEvent 直绘用）——本文件仅余此独立语义色
// 与主题查询；全局 UI 配色已于 2026-11-19 Fluent 改版（M0-M3）统一迁往
// ui/theming/FluentTheme.h（FluentTokens/fluentTheme/QSS 生成器）。
// 原 ThemeColors 配色表（"文字纯黑/纯白"浮窗约定）随迁移删除：文本灰阶
// 由 Fluent 文本四档透明度取代（2026-11-19 用户批准设计方案）。
//
// 覆盖层语义色保持独立（不随主题切换）：强调色统一为应用主色 #2B6FF2
// （此前 4 种蓝并存：#2B6FF2 / (0,120,255) / (0,150,255) / #0078FF）；
// 智能框选橙（区别于选区蓝）、标注选中绿（区别于选区语言）。
// 覆盖层恒为深色蒙版语境。
// ============================================================
namespace overlay {
inline QColor accent()      { return QColor(0x2B, 0x6F, 0xF2); } // 选区框 / 手柄 / 尺寸徽章 / 扫描线
inline QColor accentBadge() { QColor c = accent(); c.setAlpha(235); return c; }
inline QColor smartOrange() { return QColor(255, 140, 0); }      // 智能框选预高亮
inline QColor itemGreen()   { return QColor(0, 200, 120); }      // 标注选中态
inline QColor mask()        { return QColor(0, 0, 0, 105); }     // 压暗蒙版
// 日历家族共用暖强调色（恒暗语境，不随主题切换）：节日/完成勾/调整确认钮。
// 此前 (240,166,72) 三处散写无出处（T-071 收敛为具名单一来源）
inline QColor warmAccent()  { return QColor(240, 166, 72); }
} // namespace overlay

inline bool systemSchemeDark()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

} // namespace poptools
