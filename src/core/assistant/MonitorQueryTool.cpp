#include "MonitorQueryTool.h"

#include <QDate>
#include <QFile>
#include <QJsonArray>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QThreadPool>

namespace poptools {

ToolDef makeMonitorQueryTool(const QString &monitorBaseDir, QObject *ctx)
{
    ToolDef d;
    d.name = QStringLiteral("monitor.query");
    d.readOnly = true;
    d.description = QStringLiteral(
        "查询本机活动监控日志：timeline（窗口时间线，含活跃秒数/键入/点击）、"
        "keys（键入文本）、ocrtext（中文上屏识别，source=uia/ocr）、"
        "clipboard（剪贴板复制记录）。用于回答\"我今天做了什么\"、"
        "整理工作记录。stream 传 \"all\" 一次取全部（默认）。");
    d.parameters = QJsonObject{
        {"type", QStringLiteral("object")},
        {"properties",
         QJsonObject{{"date", QJsonObject{{"type", QStringLiteral("string")},
                                          {"description", QStringLiteral("日期 YYYY-MM-DD，默认今天")}}},
                     {"stream", QJsonObject{{"type", QStringLiteral("string")},
                                            {"description", QStringLiteral("timeline/keys/ocrtext/clipboard/all，默认 all")}}},
                     {"max_lines", QJsonObject{{"type", QStringLiteral("integer")},
                                               {"description", QStringLiteral("每流最多行数，默认 200")}}}}},
    };
    d.fn = [monitorBaseDir, ctx](const QJsonObject &args,
                                 const std::function<void(QJsonObject)> &done) {
        const QString date = args.value(QStringLiteral("date")).toString();
        const QString dateArg =
            date.isEmpty() ? QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")) : date;
        if (!QRegularExpression(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$")).match(dateArg).hasMatch()) {
            done(QJsonObject{{"ok", false},
                             {"error", QStringLiteral("日期格式应为 YYYY-MM-DD")}});
            return;
        }
        const QString stream = args.value(QStringLiteral("stream")).toString(QStringLiteral("all"));
        // 流名白名单：stream 原样拼进日志路径，白名单外（含 "../" 穿越
        // 片段）一律拒绝——否则该 readOnly 工具可读盘上任意 .jsonl 文件
        QStringList streams;
        if (stream == QLatin1String("all"))
            // T-066：textlog（文本镜像）流入白名单；keys 为字母流移除后的
            // 历史遗留流，仅旧日志可查
            streams << QStringLiteral("timeline") << QStringLiteral("keys")
                    << QStringLiteral("textlog")
                    << QStringLiteral("ocrtext") << QStringLiteral("clipboard");
        else if (stream == QLatin1String("timeline") || stream == QLatin1String("keys")
                 || stream == QLatin1String("textlog")
                 || stream == QLatin1String("ocrtext")
                 || stream == QLatin1String("clipboard"))
            streams << stream;
        else {
            done(QJsonObject{{"ok", false},
                             {"error",
                              QStringLiteral("stream 仅支持 timeline/keys/textlog/ocrtext/clipboard/all")}});
            return;
        }
        const int maxLines = qBound(1, args.value(QStringLiteral("max_lines")).toInt(200), 500);

        const QPointer<QObject> anchor(ctx);
        // 注：池线程里读 QPointer（if (anchor)）与 GUI 线程析构存在极窄竞争
        // 窗口——前提是锚点为模块级长命对象（GoodyModule 与进程同寿），
        // 析构只在退出瞬间；取裸指针前移交由 invokeMethod 排队，保持现状
        // 防误改（对齐 FsTool 口径）
        QThreadPool::globalInstance()->start(
            [monitorBaseDir, dateArg, streams, maxLines, anchor, done] {
                QJsonObject result{{"ok", true}, {"date", dateArg}};
                QJsonObject perStream;
                for (const QString &s : streams) {
                    QFile f(monitorBaseDir + QLatin1Char('/') + dateArg + QLatin1Char('/')
                            + s + QStringLiteral(".jsonl"));
                    QStringList lines;
                    bool truncated = false;
                    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        while (!f.atEnd() && lines.size() < maxLines) {
                            const QByteArray line = f.readLine().trimmed();
                            if (line.isEmpty())
                                continue;
                            lines << QString::fromUtf8(line.left(2000)); // 单行截断防刷屏
                        }
                        truncated = !f.atEnd();
                    }
                    perStream.insert(s, QJsonObject{{"lines", QJsonArray::fromStringList(lines)},
                                                    {"truncated", truncated}});
                }
                result.insert(QStringLiteral("streams"), perStream);
                // 回主线程交付（done 背后是模块的会话续跑逻辑）。
                // T-062 修复: 补锚点守卫对齐 FsTool——ctx 为空（单测直调）或
                // 锚点已亡时，对空对象 invokeMethod 走告警路径且 done 永不执行
                if (anchor)
                    QMetaObject::invokeMethod(
                        anchor.data(), [done, result] { done(result); },
                        Qt::QueuedConnection);
            });
    };
    return d;
}

} // namespace poptools
