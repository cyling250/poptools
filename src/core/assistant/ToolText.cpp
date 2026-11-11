#include "ToolText.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace poptools {

QString summarizeToolArgs(const QJsonObject &args)
{
    const QString raw = QString::fromUtf8(
        QJsonDocument(args).toJson(QJsonDocument::Compact));
    return raw.size() > 200 ? raw.left(200) + QStringLiteral("…") : raw;
}

QString summarizeToolResult(const QJsonObject &result)
{
    if (!result.value(QStringLiteral("ok")).toBool(true)) {
        const QString err = result.value(QStringLiteral("error")).toString();
        return err.isEmpty() ? QStringLiteral("执行失败") : err;
    }
    QString summary;
    for (auto it = result.constBegin(); it != result.constEnd(); ++it) {
        if (it.key() == QLatin1String("ok"))
            continue;
        // 字符串原样；其他类型序列化时去掉数组包装（QJsonValue 无独立序列化，
        // 套 [..] 后剥壳，避免显示成 {"v":...} 这种多余的包装结构）
        QString rendered;
        if (it.value().isString()) {
            rendered = it.value().toString();
        } else {
            rendered = QString::fromUtf8(
                           QJsonDocument(QJsonArray{it.value()}).toJson(QJsonDocument::Compact))
                           .trimmed();
            rendered = rendered.mid(1, rendered.size() - 2); // 剥 [ ]
        }
        summary += QStringLiteral("%1=%2 ").arg(it.key(), rendered);
        if (summary.size() > 160) {
            summary += QStringLiteral("…");
            break;
        }
    }
    return summary.trimmed();
}

} // namespace poptools
