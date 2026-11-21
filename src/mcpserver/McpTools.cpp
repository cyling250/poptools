#include "McpTools.h"

#include <QDateTime>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>

namespace poptools {

namespace {

constexpr int kMaxLinesCap = 500;      // 单流行数上限（防上下文爆炸）
constexpr int kLineCharCap = 2000;     // 单行截断（防刷屏）

QJsonObject err(const QString &message)
{
    return QJsonObject{{"ok", false}, {"error", message}};
}

// 日期参数校验（防路径穿越：date 原样拼进目录名）
bool validDate(const QString &date)
{
    return QRegularExpression(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"))
        .match(date)
        .hasMatch();
}

} // namespace

QJsonArray mcpToolDescriptors()
{
    // 嵌套 schema 用命名局部量构建（MSVC 对深层大括号初始化列表易炸 C2440）
    const auto strProp = [](const QString &desc) {
        return QJsonObject{{"type", QStringLiteral("string")}, {"description", desc}};
    };
    const auto intProp = [](const QString &desc) {
        return QJsonObject{{"type", QStringLiteral("integer")}, {"description", desc}};
    };
    const auto objectSchema = [](const QJsonObject &props) {
        return QJsonObject{{"type", QStringLiteral("object")},
                           {"properties", props}};
    };
    const auto tool = [](const QString &name, const QString &description,
                         const QJsonObject &schema) {
        return QJsonObject{{"name", name},
                           {"description", description},
                           {"inputSchema", schema}};
    };

    QJsonObject activityProps;
    activityProps.insert(QStringLiteral("date"),
                         strProp(QStringLiteral("日期 YYYY-MM-DD，默认今天")));
    activityProps.insert(QStringLiteral("stream"),
                         strProp(QStringLiteral("timeline/keys/textlog/ocrtext/clipboard/all，默认 all")));
    activityProps.insert(QStringLiteral("max_lines"),
                         intProp(QStringLiteral("每流最多行数，默认 200，上限 500")));

    QJsonObject writeDailyProps;
    writeDailyProps.insert(QStringLiteral("date"), strProp(QStringLiteral("日期 YYYY-MM-DD")));
    writeDailyProps.insert(QStringLiteral("markdown"),
                           strProp(QStringLiteral("日报正文（Markdown）")));

    QJsonObject readDailyProps;
    readDailyProps.insert(QStringLiteral("date"), strProp(QStringLiteral("日期 YYYY-MM-DD")));

    QJsonObject writeMonthlyProps;
    writeMonthlyProps.insert(QStringLiteral("year"), intProp(QStringLiteral("四位年份，默认当前年")));
    writeMonthlyProps.insert(QStringLiteral("month"), intProp(QStringLiteral("1-12，默认当前月")));
    writeMonthlyProps.insert(QStringLiteral("markdown"), strProp(QStringLiteral("月报正文（Markdown）")));

    QJsonObject readMonthlyProps;
    readMonthlyProps.insert(QStringLiteral("year"), intProp(QStringLiteral("四位年份，默认当前年")));
    readMonthlyProps.insert(QStringLiteral("month"), intProp(QStringLiteral("1-12，默认当前月")));

    QJsonObject ocrImageProps;
    ocrImageProps.insert(QStringLiteral("image_path"),
                         strProp(QStringLiteral("本地图片绝对路径")));

    QJsonArray tools;
    tools += tool(
        QStringLiteral("poptools_activity_daily"),
        QStringLiteral("读取本机活动监控日志（只存本机）：timeline（窗口时间线，含活跃秒数/键入/"
                       "点击）、keys（键入文本历史流）、textlog（文本镜像，拼音→汉字 replace 全程）、"
                       "ocrtext（中文上屏识别）、clipboard（剪贴板记录）。"
                       "回答「今天干了什么」或写日报时用本工具取数。"
                       "stream 传 all 一次取全部（默认）。"),
        objectSchema(activityProps));
    tools += tool(
        QStringLiteral("poptools_write_daily_digest"),
        QStringLiteral("写入指定日期的「本日工作记录」（Markdown）。这是 PopTools "
                       "桌面日历摘要链路的约定路径：写完后日历面板对应日期格会出现摘要链接。"
                       "日报任务成文后必须经本工具落盘。"),
        objectSchema(writeDailyProps));
    tools += tool(
        QStringLiteral("poptools_read_daily_digest"),
        QStringLiteral("读取指定日期的「本日工作记录」内容（月报汇总时逐日取数用）。"),
        objectSchema(readDailyProps));
    tools += tool(
        QStringLiteral("poptools_write_monthly_digest"),
        QStringLiteral("写入指定月份的工作月报（Markdown，滚动更新当月）。"
                       "月报链接展示在桌面日历左上角。"),
        objectSchema(writeMonthlyProps));
    tools += tool(
        QStringLiteral("poptools_read_monthly_digest"),
        QStringLiteral("读取指定月份的工作月报内容。"),
        objectSchema(readMonthlyProps));
    tools += tool(
        QStringLiteral("poptools_capture_screenshot"),
        QStringLiteral("截取当前屏幕（整虚拟桌面拼图），PNG 存临时目录并返回路径。"
                       "用户要求「看看我的屏幕」或结合 OCR 读取屏幕文字时用。"),
        objectSchema(QJsonObject{}));
    tools += tool(
        QStringLiteral("poptools_ocr_image"),
        QStringLiteral("对本地图片做离线 OCR（RapidOCR），返回识别文本。"
                       "常与 poptools_capture_screenshot 组合使用：先截图取路径，"
                       "再用本工具读屏幕文字。"),
        objectSchema(ocrImageProps));
    return tools;
}

McpToolResult runActivityDaily(const QString &monitorBaseDir, const QJsonObject &args)
{
    const QString date = args.value(QStringLiteral("date")).toString();
    const QString dateArg =
        date.isEmpty() ? QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")) : date;
    if (!validDate(dateArg))
        return {err(QStringLiteral("日期格式应为 YYYY-MM-DD")), true};

    const QString stream = args.value(QStringLiteral("stream")).toString(QStringLiteral("all"));
    QStringList streams;
    if (stream == QLatin1String("all"))
        streams << QStringLiteral("timeline") << QStringLiteral("keys")
                << QStringLiteral("textlog") << QStringLiteral("ocrtext")
                << QStringLiteral("clipboard");
    else if (QStringList{QStringLiteral("timeline"), QStringLiteral("keys"),
                         QStringLiteral("textlog"), QStringLiteral("ocrtext"),
                         QStringLiteral("clipboard")}
                 .contains(stream))
        streams << stream;
    else
        return {err(QStringLiteral("stream 仅支持 timeline/keys/textlog/ocrtext/clipboard/all")),
                true};

    const int maxLines = qBound(1, args.value(QStringLiteral("max_lines")).toInt(200), kMaxLinesCap);

    QJsonObject result{{"ok", true}, {"date", dateArg}};
    QJsonObject perStream;
    for (const QString &s : streams) {
        QFile f(monitorBaseDir + QLatin1Char('/') + dateArg + QLatin1Char('/') + s
                + QStringLiteral(".jsonl"));
        QStringList lines;
        bool truncated = false;
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!f.atEnd() && lines.size() < maxLines) {
                const QByteArray line = f.readLine().trimmed();
                if (line.isEmpty())
                    continue;
                lines << QString::fromUtf8(line.left(kLineCharCap));
            }
            truncated = !f.atEnd();
        }
        perStream.insert(s, QJsonObject{{"lines", QJsonArray::fromStringList(lines)},
                                        {"truncated", truncated}});
    }
    result.insert(QStringLiteral("streams"), perStream);
    return {result, false};
}

McpToolResult runWriteDailyDigest(const QString &goodyBaseDir, const QJsonObject &args)
{
    const QString date = args.value(QStringLiteral("date")).toString();
    if (!validDate(date))
        return {err(QStringLiteral("日期格式应为 YYYY-MM-DD")), true};
    const QString markdown = args.value(QStringLiteral("markdown")).toString();
    if (markdown.trimmed().isEmpty())
        return {err(QStringLiteral("markdown 不能为空")), true};
    QDir dir(goodyBaseDir + QStringLiteral("/daily"));
    if (!dir.mkpath(QStringLiteral(".")))
        return {err(QStringLiteral("目录创建失败：%1").arg(dir.absolutePath())), true};
    QFile f(dir.filePath(date + QStringLiteral(".md")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return {err(QStringLiteral("写入失败：%1").arg(f.errorString())), true};
    f.write(markdown.toUtf8());
    return {QJsonObject{{"ok", true},
                        {"path", f.fileName()},
                        {"note", QStringLiteral("日报已落盘，桌面日历该日期格将出现摘要链接")}},
            false};
}

McpToolResult runReadDailyDigest(const QString &goodyBaseDir, const QJsonObject &args)
{
    const QString date = args.value(QStringLiteral("date")).toString();
    if (!validDate(date))
        return {err(QStringLiteral("日期格式应为 YYYY-MM-DD")), true};
    QFile f(goodyBaseDir + QStringLiteral("/daily/") + date + QStringLiteral(".md"));
    if (!f.exists())
        return {QJsonObject{{"ok", true}, {"exists", false},
                            {"note", QStringLiteral("该日期尚无日报")}},
                false};
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {err(QStringLiteral("读取失败：%1").arg(f.errorString())), true};
    return {QJsonObject{{"ok", true}, {"exists", true},
                        {"markdown", QString::fromUtf8(f.readAll())}},
            false};
}

McpToolResult runWriteMonthlyDigest(const QString &goodyBaseDir, const QJsonObject &args)
{
    const QDate today = QDate::currentDate();
    const int year = args.value(QStringLiteral("year")).toInt(today.year());
    const int month = args.value(QStringLiteral("month")).toInt(today.month());
    if (month < 1 || month > 12 || year < 2000 || year > 2999)
        return {err(QStringLiteral("year/month 取值非法")), true};
    const QString markdown = args.value(QStringLiteral("markdown")).toString();
    if (markdown.trimmed().isEmpty())
        return {err(QStringLiteral("markdown 不能为空")), true};
    QDir dir(goodyBaseDir + QStringLiteral("/monthly"));
    if (!dir.mkpath(QStringLiteral(".")))
        return {err(QStringLiteral("目录创建失败：%1").arg(dir.absolutePath())), true};
    QFile f(dir.filePath(QString::number(year) + QStringLiteral("-")
                         + QString::number(month) + QStringLiteral(".md")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return {err(QStringLiteral("写入失败：%1").arg(f.errorString())), true};
    f.write(markdown.toUtf8());
    return {QJsonObject{{"ok", true},
                        {"path", f.fileName()},
                        {"note", QStringLiteral("月报已落盘，桌面日历左上角月报链接将更新")}},
            false};
}

McpToolResult runReadMonthlyDigest(const QString &goodyBaseDir, const QJsonObject &args)
{
    const QDate today = QDate::currentDate();
    const int year = args.value(QStringLiteral("year")).toInt(today.year());
    const int month = args.value(QStringLiteral("month")).toInt(today.month());
    if (month < 1 || month > 12 || year < 2000 || year > 2999)
        return {err(QStringLiteral("year/month 取值非法")), true};
    QFile f(goodyBaseDir + QStringLiteral("/monthly/") + QString::number(year)
            + QStringLiteral("-") + QString::number(month) + QStringLiteral(".md"));
    if (!f.exists())
        return {QJsonObject{{"ok", true}, {"exists", false},
                            {"note", QStringLiteral("该月份尚无月报")}},
                false};
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {err(QStringLiteral("读取失败：%1").arg(f.errorString())), true};
    return {QJsonObject{{"ok", true}, {"exists", true},
                        {"markdown", QString::fromUtf8(f.readAll())}},
            false};
}

McpToolResult runCaptureScreenshot(const QJsonObject &args)
{
    Q_UNUSED(args);
    // 整虚拟桌面拼图：各屏 grabWindow 后按屏几何偏移贴入画布（T-066 教训：
    // 多屏拼接必须以虚拟桌面坐标为基准，ImageGrab all_screens 同理）
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty())
        return {err(QStringLiteral("无可用屏幕")), true};
    QRect virtualGeo = screens.first()->geometry();
    for (const QScreen *s : screens)
        virtualGeo = virtualGeo.united(s->geometry());
    QImage canvas(virtualGeo.size(), QImage::Format_RGB32);
    canvas.fill(Qt::black);
    QPainter painter(&canvas);
    for (QScreen *s : screens) { // grabWindow 非 const 成员，须可变遍历
        const QPixmap pm = s->grabWindow(0); // 全屏
        painter.drawPixmap(s->geometry().topLeft() - virtualGeo.topLeft(), pm);
    }
    painter.end();
    const QString path = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                         + QStringLiteral("/poptools_mcp_capture_%1.png")
                               .arg(QDateTime::currentMSecsSinceEpoch());
    if (!canvas.save(path, "PNG"))
        return {err(QStringLiteral("PNG 保存失败")), true};
    return {QJsonObject{{"ok", true},
                        {"path", path},
                        {"width", canvas.width()},
                        {"height", canvas.height()},
                        {"note", QStringLiteral("已截取整虚拟桌面；可用本机工具打开或对路径做 OCR")}},
            false};
}

} // namespace poptools
