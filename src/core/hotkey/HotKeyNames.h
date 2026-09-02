#pragma once

#include <QString>
#include <QtGlobal>

namespace poptools {

// ============================================================
// 热键显示名：VK + 修饰键 → "Ctrl+Alt+Shift+X" / "F1" 等人类可读文案。
// 用于托盘菜单文案与热键注册失败的气泡提示。
//
// 修饰键取值与 Win32 MOD_ALT/CONTROL/SHIFT/WIN（1/2/4/8）一致；
// 刻意不 include windows.h，避免其宏污染包含本头的翻译单元。
// 未覆盖的键回退为十六进制码。
// ============================================================
inline QString hotKeyDisplayName(quint32 vk, quint32 modifiers = 0)
{
    QString prefix;
    if (modifiers & 0x2) prefix += QStringLiteral("Ctrl+");
    if (modifiers & 0x1) prefix += QStringLiteral("Alt+");
    if (modifiers & 0x4) prefix += QStringLiteral("Shift+");
    if (modifiers & 0x8) prefix += QStringLiteral("Win+");

    QString key;
    if (vk >= 0x70 && vk <= 0x87) {          // F1–F24
        key = QStringLiteral("F%1").arg(vk - 0x70 + 1);
    } else if (vk >= 0x41 && vk <= 0x5A) {   // A–Z
        key = QChar::fromLatin1(char(vk));
    } else if (vk >= 0x30 && vk <= 0x39) {   // 0–9
        key = QChar::fromLatin1(char(vk));
    } else if (vk >= 0x60 && vk <= 0x69) {   // 小键盘 0–9
        key = QStringLiteral("Num%1").arg(vk - 0x60);
    } else {
        switch (vk) {
            case 0x08: key = QStringLiteral("Backspace"); break;
            case 0x09: key = QStringLiteral("Tab"); break;
            case 0x0D: key = QStringLiteral("Enter"); break;
            case 0x13: key = QStringLiteral("Pause"); break;
            case 0x14: key = QStringLiteral("CapsLock"); break;
            case 0x1B: key = QStringLiteral("Esc"); break;
            case 0x20: key = QStringLiteral("Space"); break;
            case 0x21: key = QStringLiteral("PageUp"); break;
            case 0x22: key = QStringLiteral("PageDown"); break;
            case 0x23: key = QStringLiteral("End"); break;
            case 0x24: key = QStringLiteral("Home"); break;
            case 0x25: key = QStringLiteral("Left"); break;
            case 0x26: key = QStringLiteral("Up"); break;
            case 0x27: key = QStringLiteral("Right"); break;
            case 0x28: key = QStringLiteral("Down"); break;
            case 0x2C: key = QStringLiteral("PrintScreen"); break;
            case 0x2D: key = QStringLiteral("Insert"); break;
            case 0x2E: key = QStringLiteral("Delete"); break;
            default: {
                // 仅十六进制位大写（对整个串 toUpper 会把 "0x" 变成 "0X"）
                const QString hex = QString::number(vk, 16).toUpper();
                key = QStringLiteral("0x") + hex.rightJustified(2, QLatin1Char('0'));
                break;
            }
        }
    }
    return prefix + key;
}

} // namespace poptools
