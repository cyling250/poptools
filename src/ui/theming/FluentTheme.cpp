#include "FluentTheme.h"

#include <QColor>
#include <QWidget>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>
#endif

namespace poptools {

QFont uiFluentFont(int pixelSize, QFont::Weight weight)
{
    QFont f;
    f.setFamilies({QStringLiteral("Segoe UI Variable Text"),
                   QStringLiteral("Segoe UI"),
                   QStringLiteral("Microsoft YaHei UI")});
    f.setPixelSize(pixelSize);
    f.setWeight(weight);
    return f;
}

// 明暗双套数值速查（Win11 官方 token 值 + 品牌主色）：
//   文本四档 = Fluent 官方透明度档（浅色：黑 89/62/45/36%；深色：白 100/77/53/36%）
//   预混到不透明 hex（浅色按白底、深色按 #202020 底），QSS 直接可用
FluentTokens fluentTheme(bool dark)
{
    FluentTokens t;
    t.dark = dark;
    if (dark) {
        t.bgBase = "#202020";       t.bgCard = "#2A2A2A";
        t.bgCardHover = "#313131";  t.bgSubtle = "#1C1C1C";
        t.cardStroke = "#383838";   t.divider = "#333333";
        t.textPrimary = "#FFFFFF";  t.textSecondary = "#CBCBCB";
        t.textTertiary = "#969696"; t.textDisabled = "#6B6B6B";
        t.ctrlBg = "#323232";       t.ctrlBorder = "#454545";
        t.ctrlHover = "#3D3D3D";    t.ctrlPressed = "#2A2A2A";
        t.accent = "#5B93FF";       t.accentHover = "#79A6FF";
        t.accentPressed = "#4A80F5"; t.accentSoft = "#2E5B93FF";
        t.onAccent = "#FFFFFF";
        t.errText = "#FF6B6B";      t.errBg = "#2A1414";
        t.successText = "#4CC38A";
        t.scrollThumb = "#4D4D4D";  t.scrollThumbHover = "#666666";
    } else {
        t.bgBase = "#F3F3F3";       t.bgCard = "#FFFFFF";
        t.bgCardHover = "#F9F9F9";  t.bgSubtle = "#EEEEEE";
        t.cardStroke = "#E4E4E4";   t.divider = "#E8E8E8";
        t.textPrimary = "#1B1B1B";  t.textSecondary = "#616161";
        t.textTertiary = "#8C8C8C"; t.textDisabled = "#A6A6A6";
        t.ctrlBg = "#FFFFFF";       t.ctrlBorder = "#D6D6D6";
        t.ctrlHover = "#F4F4F4";    t.ctrlPressed = "#EBEBEB";
        t.accent = "#2B6FF2";       t.accentHover = "#3F7DF3";
        t.accentPressed = "#1A5CDB"; t.accentSoft = "#1F2B6FF2";
        t.onAccent = "#FFFFFF";
        t.errText = "#C0392B";      t.errBg = "#FDF0EF";
        t.successText = "#1E8E3E";
        t.scrollThumb = "#C8C8C8";  t.scrollThumbHover = "#ABABAB";
    }
    return t;
}

QString fluentInputQss(const FluentTokens &t)
{
    // radiusControl=4、padding 5px 10px（≈控件高 32px，Win11 密度）
    return QStringLiteral(
        "QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QComboBox {"
        "  background: %1; border: 1px solid %2; border-radius: 4px;"
        "  padding: 4px 10px; color: %3; selection-background-color: %4; }"
        "QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus,"
        "QSpinBox:focus, QComboBox:focus {"
        "  border-color: %4; background: %5; }"
        "QLineEdit:disabled, QPlainTextEdit:disabled, QTextEdit:disabled,"
        "QSpinBox:disabled, QComboBox:disabled {"
        "  color: %6; background: %5; }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox QAbstractItemView { background: %7; color: %3;"
        "  border: 1px solid %8; border-radius: 4px;"
        "  selection-background-color: %9; }")
        .arg(t.ctrlBg, t.ctrlBorder, t.textPrimary, t.accent, t.bgSubtle,
             t.textDisabled, t.bgCard, t.cardStroke, t.ctrlHover);
}

QString fluentButtonQss(const FluentTokens &t)
{
    return QStringLiteral(
        "QPushButton { background: %1; border: 1px solid %2; border-radius: 4px;"
        "  padding: 5px 14px; color: %3; }"
        "QPushButton:hover { background: %4; }"
        "QPushButton:pressed { background: %5; }"
        "QPushButton:disabled { color: %6; background: %7; border-color: %8; }"
        "QPushButton#accentBtn { background: %9; color: %10; border-color: %9;"
        "  font-weight: 600; }"
        "QPushButton#accentBtn:hover { background: %11; border-color: %11; }"
        "QPushButton#accentBtn:pressed { background: %12; border-color: %12; }"
        "QPushButton#accentBtn:disabled { background: %7; border-color: %8;"
        "  color: %6; font-weight: 600; }")
        .arg(t.ctrlBg, t.ctrlBorder, t.textPrimary, t.ctrlHover, t.ctrlPressed,
             t.textDisabled, t.bgSubtle, t.cardStroke, t.accent, t.onAccent,
             t.accentHover, t.accentPressed);
}

QString fluentScrollBarQss(const FluentTokens &t)
{
    return QStringLiteral(
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: %1; border-radius: 3px;"
        "  min-height: 32px; }"
        "QScrollBar::handle:vertical:hover { background: %2; }"
        "QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }"
        "QScrollBar::handle:horizontal { background: %1; border-radius: 3px;"
        "  min-width: 32px; }"
        "QScrollBar::handle:horizontal:hover { background: %2; }"
        "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }")
        .arg(t.scrollThumb, t.scrollThumbHover);
}

void applyFluentWindowChrome(QWidget *window, bool dark)
{
#ifdef Q_OS_WIN
    if (!window)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd)
        return;
    // 深浅色标题栏（属性 20，Win10 1809+；旧系统返回错误码，静默忽略）
    const BOOL darkVal = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkVal,
                          sizeof(darkVal));
    // Mica backdrop（属性 38 = DWMSBT_MAINWINDOW，Win11 22H2+；旧系统/失败
    // 静默回退：窗口底色取 bgBase 官方回退固色即可）
    const int backdrop = 2;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop,
                          sizeof(backdrop));
#else
    Q_UNUSED(window);
    Q_UNUSED(dark);
#endif
}

} // namespace poptools
