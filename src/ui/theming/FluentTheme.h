#pragma once

#include <QFont>
#include <QString>

class QWidget;

namespace poptools {

// ============================================================
// Fluent 主题 token 体系（2026-11-19 UI 现代化改版 M0 底座）。
//
// 设计基准：Windows 11 Fluent Design 官方规范（圆角 8/4px、4px 间距网格、
// Segoe UI Variable 字体刻度、明暗双套色板），主色保留品牌蓝 #2B6FF2。
// 数值速查见 docs/design/20261119-ui-fluent-restyle-proposal.md 第三节。
//
// 与 ThemeColors 的关系：ThemeColors 是 2026-09 起翻译类浮窗的旧配色表
// （"文字一律纯黑/纯白"约定），继续服务于既有浮窗；本表是全局新基准，
// 存量窗口按 M1（设置）→ M2（Goody）→ M3（其余）分期迁移。
// 注意：旧"不允许灰字"约定自改版起由 Fluent 文本四档透明度取代
//（用户 2026-11-19 批准设计方案）。
//
// 使用纪律（沿用 P-022/P-023）：
//   * 自定义类一律 objectName + ID 选择器；Qt 内置类可走类型选择器；
//   * 样式一律从 fluentTheme() 取值，禁止在窗口代码里写十六进制字面量
//     （贴桌面玻璃家族的半透明描边除外——alpha 语义色，注释声明）；
//   * 常暗语境（截图覆盖层/贴桌面面板上的浮层）取 fluentTheme(true) 固定
//     深色套，不随系统明暗切换（先例：capture ToolBar 常暗玻璃面板）；
//   * **已定案豁免**：① ui/dsh/GoodyTheme.h 是 Goody 窗的第二 token 表
//     （T-070 按 dsh WebUI 视觉拍板，两表互不引用）——Goody 窗内配色取
//     goody_theme，不入本表口径；② 内容惯例色（ToolBar 16 色绘图调色盘、
//     CodeHighlighter 语法配色、取色卡白底信息区）是用户内容/功能语义，
//     非 UI 配色；③ 无壁纸时的回退渐变底（DesktopWallpaperProvider）是
//     内容画面兜底。以上均注释声明，不视为违反本纪律。
// ============================================================

// ---- 结构刻度（Win11 Geometry / Content layout 官方规范） ----
namespace fluent {
constexpr int kRadiusOverlay = 8; // 窗口、对话框、卡片容器、弹层
constexpr int kRadiusControl = 4; // 按钮、输入框、下拉、复选框
constexpr int kRadiusPanel = 12;  // 贴桌面玻璃面板（signature 圆角，保留）

constexpr int kSpaceXS = 8;  // 控件之间
constexpr int kSpaceS = 12;  // 标签与控件、卡片组之间
constexpr int kSpaceM = 16;  // 卡片内边距、边缘留白
constexpr int kSpaceL = 24;  // 分区之间

// 字号刻度（px；行高由控件布局自理）。强调一律 Semibold 不用 Bold。
constexpr int kFontCaption = 12;    // 辅助说明、时间戳
constexpr int kFontBody = 14;       // 正文、控件
constexpr int kFontBodyLarge = 18;  // 页面引言
constexpr int kFontSubtitle = 20;   // 页标题
} // namespace fluent

// ---- 字体 ----
// 字体栈：西文/数字 Segoe UI Variable（Win11）/ Segoe UI，中文回退微软雅黑。
// weight 传 QFont::DemiBold 表达 Semibold(600)。
QFont uiFluentFont(int pixelSize, QFont::Weight weight = QFont::Normal);

// ---- 明暗双套 token ----
struct FluentTokens
{
    bool dark = false;

    // 基底与表面（Win11 SolidBackground/CardBackground 官方值）
    const char *bgBase = nullptr;      // 窗口基底（Mica 回退固色）
    const char *bgCard = nullptr;      // 卡片底
    const char *bgCardHover = nullptr; // 卡片悬停底
    const char *bgSubtle = nullptr;    // 次级表面（输入 focus 态底、禁用态底）
    const char *cardStroke = nullptr;  // 卡片 1px 描边
    const char *divider = nullptr;     // 分隔线

    // 文本四档透明度（Fluent Typography 官方档位）
    const char *textPrimary = nullptr;
    const char *textSecondary = nullptr;
    const char *textTertiary = nullptr;
    const char *textDisabled = nullptr;

    // 控件（次级按钮/输入框/下拉）
    const char *ctrlBg = nullptr;
    const char *ctrlBorder = nullptr;
    const char *ctrlHover = nullptr;
    const char *ctrlPressed = nullptr;

    // 主色（品牌蓝；深色主题整体提亮一档保证对比）
    const char *accent = nullptr;
    const char *accentHover = nullptr;
    const char *accentPressed = nullptr;
    const char *accentSoft = nullptr; // #AARRGGBB 低透明主色（导航选中底/选区底）
    const char *onAccent = nullptr;   // accent 实底上的文字

    // 语义色
    const char *errText = nullptr;
    const char *errBg = nullptr;
    const char *successText = nullptr;

    // 滚动条
    const char *scrollThumb = nullptr;
    const char *scrollThumbHover = nullptr;
};

FluentTokens fluentTheme(bool dark);

// ---- 通用控件 QSS 片段（类型选择器；拼进窗口级样式表作控件基线） ----

// 输入族：QLineEdit / QPlainTextEdit / QTextEdit / QSpinBox / QComboBox。
// focus 态 = accent 描边 + bgSubtle 底（Win11 focus 降底色特征）
QString fluentInputQss(const FluentTokens &t);

// 次级按钮基线。主行动按钮不在此：objectName 设 "accentBtn" 即命中
// 本片段的 #accentBtn 规则（accent 实底 + Semibold）
QString fluentButtonQss(const FluentTokens &t);

// 细滚动条（10px 胶囊手柄，轨道透明）
QString fluentScrollBarQss(const FluentTokens &t);

// ---- 窗口 chrome（Win11） ----
// 深浅色标题栏（DWMWA_USE_IMMERSIVE_DARK_MODE，Win10 1809+ 生效）+ Mica
// backdrop（DWMWA_SYSTEMBACKDROP_TYPE，Win11 22H2+ 生效，失败静默回退）。
// 说明：Qt 默认 backing store 不透明，Mica 只在窗口背景 alpha<255 的区域
// 可见；窗口底色取 t.bgBase（官方 Mica 回退固色）时观感与"关闭透明效果的
// Win11 系统窗口"一致。调用时机：窗口 winId() 可得后（show 之前/之后均可）。
void applyFluentWindowChrome(QWidget *window, bool dark);

} // namespace poptools
