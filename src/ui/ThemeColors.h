#pragma once

#include <QGuiApplication>
#include <QStyleHints>
#include <QtGlobal>

namespace poptools {

// ============================================================
// 翻译类浮窗（划词翻译卡片 / OCR 文字栏）的明暗主题配色表。
//
// 颜色语义（用户约定，2026-09-03 修订）：
//   文字一律纯黑（浅色主题）/ 纯白（深色主题），不允许灰字
//   （含标题、小节提示、正文、按钮文字）；灰色仅用于线框——
//   卡片边框、文本区边框、分隔线。
//
// 这类浮窗显示在截图覆盖层之上，配色不能依赖系统调色板，
// 统一从本表取值后经 setStyleSheet 应用；跟随系统明暗切换时
// 重新取表应用即可（监听 QStyleHints::colorSchemeChanged）。
// ============================================================
struct ThemeColors
{
    bool dark = false;
    const char *cardBg = nullptr;       // 卡片底色
    const char *cardBorder = nullptr;   // 卡片外边框（灰）
    const char *title = nullptr;        // 标题文字（纯黑/纯白）
    const char *sectionTitle = nullptr; // 小节提示文字（纯黑/纯白）
    const char *body = nullptr;         // 正文文字（纯黑/纯白）
    const char *editBg = nullptr;       // 文本区底色
    const char *editBorder = nullptr;   // 文本区边框（灰）
    const char *ctrlBg = nullptr;       // 普通按钮底色
    const char *ctrlBorder = nullptr;   // 普通按钮边框（灰）
    const char *ctrlText = nullptr;     // 普通按钮文字（纯黑/纯白）
    const char *ctrlHover = nullptr;    // 普通按钮悬停底色
    const char *ctrlDisabledText = nullptr; // 禁用态文字（同样纯黑/纯白，本应用无灰字）
    const char *line = nullptr;         // 分隔线（灰）
    const char *errText = nullptr;      // 错误态文字
    const char *errBg = nullptr;        // 错误态底色
    const char *errBorder = nullptr;    // 错误态边框
    const char *closeText = nullptr;    // 关闭按钮文字（纯黑/纯白）
    const char *closeHover = nullptr;   // 关闭按钮悬停文字（强调蓝）
};

inline bool systemSchemeDark()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

inline ThemeColors uiThemeColors(bool dark)
{
    ThemeColors c;
    c.dark = dark;
    if (dark) {
        c.cardBg = "#1E1E1E";       c.cardBorder = "#3C3C3C";
        c.title = "#FFFFFF";        c.sectionTitle = "#FFFFFF";  c.body = "#FFFFFF";
        c.editBg = "#0D0D0D";       c.editBorder = "#2A2A2A";
        c.ctrlBg = "#2A2A2A";       c.ctrlBorder = "#3C3C3C";   c.ctrlText = "#FFFFFF";
        c.ctrlHover = "#3C3C3C";    c.ctrlDisabledText = "#FFFFFF";
        c.line = "#2A2A2A";
        c.errText = "#FF6B6B";      c.errBg = "#2A0000";        c.errBorder = "#5C0000";
        c.closeText = "#FFFFFF";    c.closeHover = "#2B6FF2";
    } else {
        c.cardBg = "#FFFFFF";       c.cardBorder = "#D9D9D9";
        c.title = "#000000";        c.sectionTitle = "#000000";  c.body = "#000000";
        c.editBg = "#FFFFFF";       c.editBorder = "#D9D9D9";
        c.ctrlBg = "#FFFFFF";       c.ctrlBorder = "#D9D9D9";   c.ctrlText = "#000000";
        c.ctrlHover = "#F0F0F0";    c.ctrlDisabledText = "#000000";
        c.line = "#E0E0E0";
        c.errText = "#C0392B";      c.errBg = "#FDF0EF";        c.errBorder = "#F5B7B1";
        c.closeText = "#000000";    c.closeHover = "#2B6FF2";
    }
    return c;
}

} // namespace poptools
