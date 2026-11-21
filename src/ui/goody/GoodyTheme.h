#pragma once

#include <QColor>

#include "ui/ThemeColors.h" // systemSchemeDark

namespace poptools {

// ============================================================
// GoodyTheme：dsh WebUI 设计 token 的 Qt 常量表（T-070 Goody 前端重构）。
//
// 数值取自 deepseek-harness packages/client/ui-theme/src/styles/
// design-platform.css 与 base.css（2026-11 调研快照，MIT 许可）。
// 与全局 FluentTheme（src/ui/theming）的关系：Fluent 是全应用基准，
// Goody 窗按用户拍板对齐 dsh 视觉，独立取本表；两表互不引用。
// ============================================================
namespace goody_theme {

// ---- 明暗选择 ----
inline bool dark() { return systemSchemeDark(); }

// ---- 基底与表面（neutral-bluish 系） ----
inline QColor bgBase()      { return dark() ? QColor(0x15, 0x15, 0x17) : QColor(0xFF, 0xFF, 0xFF); } // 950 / 00
inline QColor bgSidebar()   { return dark() ? QColor(0x1C, 0x1C, 0x1E) : QColor(0xF5, 0xF7, 0xFB); } // 900 / 50 近似
inline QColor bgCard()      { return dark() ? QColor(0x2C, 0x2C, 0x2E) : QColor(0xFF, 0xFF, 0xFF); } // 卡/气泡底
inline QColor bgHover()     { return dark() ? QColor(0x35, 0x35, 0x38) : QColor(0xEE, 0xF1, 0xF7); }
inline QColor inputBg()     { return dark() ? QColor(0x26, 0x26, 0x28) : QColor(0xF7, 0xF8, 0xFA); } // composer 内底

// ---- 品牌蓝（deepseek 系） ----
inline QColor accent()        { return dark() ? QColor(86, 134, 254) : QColor(65, 118, 230); }  // 450 / 500
inline QColor accentHover()   { return dark() ? QColor(122, 170, 255) : QColor(86, 134, 254); } // 400 / 450
inline QColor accentSoft()    { return dark() ? QColor(86, 134, 254, 45) : QColor(65, 118, 230, 26); } // 低透明主色（侧栏选中底/示例胶囊 hover 底）
inline QColor userBubbleBg()  { return dark() ? QColor(44, 44, 46) : QColor(237, 243, 254); }   // 850 / deepseek-50
inline QColor onAccent()      { return QColor(0xFF, 0xFF, 0xFF); }                              // accent 实底上的文字

// ---- 文本（primary/secondary/tertiary 两档透明度预混） ----
inline QColor textPrimary()   { return dark() ? QColor(0xEC, 0xEC, 0xEC) : QColor(0x1B, 0x1B, 0x1F); } // 1000/50
inline QColor textSecondary() { return dark() ? QColor(0xA6, 0xA6, 0xAC) : QColor(0x5C, 0x5F, 0x66); } // 700/300
inline QColor textTertiary()  { return dark() ? QColor(0x7A, 0x7A, 0x80) : QColor(0x8A, 0x8D, 0x94); } // 600/400

// ---- 状态色 ----
inline QColor error()   { return dark() ? QColor(0xF8, 0x71, 0x71) : QColor(0xDC, 0x26, 0x26); } // red-600/400
inline QColor warning() { return QColor(0xF5, 0x9E, 0x0B); }                                     // amber-500
inline QColor success() { return QColor(0x16, 0xA3, 0x4A); }                                     // green-500

// ---- 描边（四级 alpha 黑/白） ----
inline QColor stroke()      { return dark() ? QColor(255, 255, 255, 31) : QColor(0, 0, 0, 26); }   // 0.12/0.10
inline QColor strokeSoft()  { return dark() ? QColor(255, 255, 255, 15) : QColor(0, 0, 0, 10); }   // 0.06/0.04

// ---- 圆角（base.css：xs4 sm8 md12 lg16 xl20 panel28） ----
constexpr int kRadiusSm = 8;
constexpr int kRadiusMd = 12;
constexpr int kRadiusLg = 16;
constexpr int kRadiusXl = 20;
constexpr int kRadiusPanel = 24; // composer 卡（官方 28 在 Qt 视觉偏大，收 24）

// ---- 字号（px；dsh 正文 14/24 次级 13 过程行 13 banner 11） ----
constexpr int kFontBanner = 11;
constexpr int kFontCaption = 12;
constexpr int kFontProcess = 13;
constexpr int kFontBody = 14;
constexpr int kFontTitle = 15;

// ---- 布局常量 ----
constexpr int kSidebarWidth = 280;      // 会话侧栏（dsh 默认 280）
constexpr int kMsgColumnMax = 880;      // 消息列宽上限（clamp 680~880）
constexpr int kMsgGap = 14;             // 文档流消息间距（dsh 消息块间 14~16px）
constexpr int kComposerMaxHeight = 200; // 输入卡内容最大高（dsh 336px 对应约 14 行，
                                        // 桌面窗体矮一档收 200，超出内部滚动）
constexpr int kBodyLineHeightPct = 150; // 正文/思考区行距（dsh 14/24≈171%，Qt 块格式
                                        // 取 150% 折中：放宽阅读节奏又不显松散）

} // namespace goody_theme

} // namespace poptools
