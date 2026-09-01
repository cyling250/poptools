#pragma once

#include <QString>
#include <QStringList>

namespace snipaste {

// ============================================================
// 语言下拉框的显示名与语言代码映射（TranslateCard / OcrPanel 共用）
// 代码值与 Translator::langName 的分支保持一致
// ============================================================

// 下拉框显示名（首项"自动"仅用于源语言）
inline QStringList languageDisplays()
{
    return {QStringLiteral("自动"), QStringLiteral("中文"), QStringLiteral("英文"),
            QStringLiteral("日文"), QStringLiteral("韩文"), QStringLiteral("法文"),
            QStringLiteral("德文"), QStringLiteral("西文"), QStringLiteral("俄文")};
}

// 显示名 → 语言代码；无法识别时原样返回（兼容 OcrPanel 可编辑下拉框中
// 用户自行输入的自定义语言代码，如 "pt"）
inline QString displayToLangCode(const QString &display)
{
    const QString s = display.trimmed();
    if (s.contains(QStringLiteral("自动"))) return QStringLiteral("auto");
    if (s.contains(QStringLiteral("中"))) return QStringLiteral("zh");
    if (s.contains(QStringLiteral("英"))) return QStringLiteral("en");
    if (s.contains(QStringLiteral("日"))) return QStringLiteral("ja");
    if (s.contains(QStringLiteral("韩"))) return QStringLiteral("ko");
    if (s.contains(QStringLiteral("法"))) return QStringLiteral("fr");
    if (s.contains(QStringLiteral("德"))) return QStringLiteral("de");
    if (s.contains(QStringLiteral("西"))) return QStringLiteral("es");
    if (s.contains(QStringLiteral("俄"))) return QStringLiteral("ru");
    return s;
}

// 语言代码 → 显示名（用于从设置恢复下拉框选中项）；
// 未知代码原样返回：非可编辑下拉框 setCurrentText 不命中时保持当前项，
// 可编辑下拉框（OcrPanel 目标语言）则直接显示该代码
inline QString langCodeToDisplay(const QString &code)
{
    if (code == QStringLiteral("auto")) return QStringLiteral("自动");
    if (code == QStringLiteral("zh")) return QStringLiteral("中文");
    if (code == QStringLiteral("en")) return QStringLiteral("英文");
    if (code == QStringLiteral("ja")) return QStringLiteral("日文");
    if (code == QStringLiteral("ko")) return QStringLiteral("韩文");
    if (code == QStringLiteral("fr")) return QStringLiteral("法文");
    if (code == QStringLiteral("de")) return QStringLiteral("德文");
    if (code == QStringLiteral("es")) return QStringLiteral("西文");
    if (code == QStringLiteral("ru")) return QStringLiteral("俄文");
    return code;
}

} // namespace snipaste
