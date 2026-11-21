#include "McpTools.h"

#include "core/box/BoxCatalog.h"
#include "core/calendar/CalendarSettings.h"
#include "core/calendar/CalendarTaskStore.h"
#include "core/translation/Translator.h"
#include "core/settings/Settings.h"

#include <QDate>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

namespace poptools {

namespace {

QJsonObject err(const QString &message)
{
    return QJsonObject{{"ok", false}, {"error", message}};
}

// dirty 触碰（ExternalSyncWatcher 监听数据目录，读后即删协议）
void touchDirty(const QString &dataRoot, const QString &name)
{
    QFile f(dataRoot + QLatin1Char('/') + name);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QByteArray::number(QDateTime::currentMSecsSinceEpoch()));
}

QJsonObject calendarSettingsJson(const CalendarSettings &cal)
{
    return QJsonObject{
        {"show_lunar", cal.showLunar},
        {"show_week_numbers", cal.showWeekNumbers},
        {"show_events", cal.showEvents},
        {"glass", cal.glassEffect},
        {"show_guide", cal.showGuide},
        {"show_list", !cal.hidden},
        {"font_scale", cal.fontScale},
    };
}

QJsonObject tasksJsonForRange(const CalendarTaskStore &store, const QDate &from, const QDate &to)
{
    // 逐日收集（范围通常数日～数月；CalendarTaskStore::all 无序，按日期升序输出）
    QJsonObject days;
    for (QDate d = from; d <= to; d = d.addDays(1)) {
        const QVector<CalendarTaskItem> items = store.get(d);
        if (items.isEmpty())
            continue;
        QJsonArray arr;
        for (const CalendarTaskItem &it : items)
            arr.append(QJsonObject{{"text", it.text},
                                   {"done", it.done},
                                   {"time", it.time}});
        days.insert(CalendarTaskStore::keyFor(d), arr);
    }
    return QJsonObject{{"ok", true},
                       {"from", from.toString(QStringLiteral("yyyy-MM-dd"))},
                       {"to", to.toString(QStringLiteral("yyyy-MM-dd"))},
                       {"days", days}};
}

// 日报/月报清单（上下文管理：AI 先查目录，再按需读单篇）
QJsonArray digestInventory(const QString &goodyBaseDir)
{
    QJsonArray out;
    const auto addDir = [&out](const QDir &d, const QString &kind) {
        for (const QFileInfo &fi : d.entryInfoList({"*.md"}, QDir::Files, QDir::Name)) {
            QFile f(fi.absoluteFilePath());
            QString title;
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                title = QString::fromUtf8(f.readLine()).trimmed().left(80);
            }
            out.append(QJsonObject{{"kind", kind},
                                   {"name", fi.fileName()},
                                   {"path", fi.absoluteFilePath()},
                                   {"size", double(fi.size())},
                                   {"modified", fi.lastModified().toString(Qt::ISODate)},
                                   {"title", title}});
        }
    };
    addDir(QDir(goodyBaseDir + QStringLiteral("/daily")), QStringLiteral("daily"));
    addDir(QDir(goodyBaseDir + QStringLiteral("/monthly")), QStringLiteral("monthly"));
    return out;
}

} // namespace

QJsonArray domainToolDescriptors()
{
    const auto strProp = [](const QString &desc) {
        return QJsonObject{{"type", QStringLiteral("string")}, {"description", desc}};
    };
    const auto intProp = [](const QString &desc) {
        return QJsonObject{{"type", QStringLiteral("integer")}, {"description", desc}};
    };
    const auto boolProp = [](const QString &desc) {
        return QJsonObject{{"type", QStringLiteral("boolean")}, {"description", desc}};
    };
    const auto objectSchema = [](const QJsonObject &props) {
        return QJsonObject{{"type", QStringLiteral("object")}, {"properties", props}};
    };
    const auto tool = [](const QString &name, const QString &description,
                         const QJsonObject &schema) {
        return QJsonObject{{"name", name},
                           {"description", description},
                           {"inputSchema", schema}};
    };

    QJsonArray tools;

    // ---- 日历域 ----
    QJsonObject tasksGetProps;
    tasksGetProps.insert(QStringLiteral("date_start"), strProp(QStringLiteral("起始日期 YYYY-MM-DD，默认今天")));
    tasksGetProps.insert(QStringLiteral("date_end"), strProp(QStringLiteral("结束日期 YYYY-MM-DD，默认同 start")));
    tools += tool(QStringLiteral("poptools_calendar_tasks_get"),
                  QStringLiteral("读取桌面日历日程（按日期范围）。日程也用于回答「昨天有什么计划/接下来要做什么」。"),
                  objectSchema(tasksGetProps));
    QJsonObject taskAddProps;
    taskAddProps.insert(QStringLiteral("date"), strProp(QStringLiteral("日期 YYYY-MM-DD")));
    taskAddProps.insert(QStringLiteral("text"), strProp(QStringLiteral("日程内容")));
    taskAddProps.insert(QStringLiteral("time"), strProp(QStringLiteral("可选 HH:mm 时间前缀")));
    tools += tool(QStringLiteral("poptools_calendar_task_add"),
                  QStringLiteral("新增一条日程（写入桌面日历，立即生效）。替用户记日程/安排计划时用。"),
                  objectSchema(taskAddProps));
    QJsonObject taskDelProps;
    taskDelProps.insert(QStringLiteral("date"), strProp(QStringLiteral("日期 YYYY-MM-DD")));
    taskDelProps.insert(QStringLiteral("match"), strProp(QStringLiteral("日程文本子串（删除第一条匹配）")));
    tools += tool(QStringLiteral("poptools_calendar_task_delete"),
                  QStringLiteral("删除一条日程（按文本子串匹配第一条）。"),
                  objectSchema(taskDelProps));
    tools += tool(QStringLiteral("poptools_calendar_settings_get"),
                  QStringLiteral("读取桌面日历显示设置（农历/周数/日程/毛玻璃/字体缩放等）。"),
                  objectSchema(QJsonObject{}));
    QJsonObject calSetProps;
    calSetProps.insert(QStringLiteral("show_lunar"), boolProp(QStringLiteral("显示农历/节气/节日")));
    calSetProps.insert(QStringLiteral("show_week_numbers"), boolProp(QStringLiteral("显示周数")));
    calSetProps.insert(QStringLiteral("show_events"), boolProp(QStringLiteral("显示日程内容")));
    calSetProps.insert(QStringLiteral("glass"), boolProp(QStringLiteral("毛玻璃特效")));
    calSetProps.insert(QStringLiteral("font_scale"), intProp(QStringLiteral("字体缩放 70~160（%）")));
    calSetProps.insert(QStringLiteral("show_list"), boolProp(QStringLiteral("显示日历清单（false=从桌面隐藏）")));
    tools += tool(QStringLiteral("poptools_calendar_settings_set"),
                  QStringLiteral("修改桌面日历显示设置（只传要改的字段，立即生效）。"
                                 "用户说「把日历的农历关掉」这类需求时用。"),
                  objectSchema(calSetProps));

    // ---- 活动监控域 ----
    tools += tool(QStringLiteral("poptools_activity_settings_get"),
                  QStringLiteral("读取活动监控设置（总开关/OCR 快照/空闲阈值/保留天数）。"),
                  objectSchema(QJsonObject{}));
    QJsonObject actSetProps;
    actSetProps.insert(QStringLiteral("enabled"), boolProp(QStringLiteral("总开关")));
    actSetProps.insert(QStringLiteral("ocr_snapshot"), boolProp(QStringLiteral("中文上屏 OCR 快照")));
    actSetProps.insert(QStringLiteral("idle_sec"), intProp(QStringLiteral("判定离开的空闲秒数（10~3600）")));
    actSetProps.insert(QStringLiteral("retention_days"), intProp(QStringLiteral("日志保留天数")));
    tools += tool(QStringLiteral("poptools_activity_settings_set"),
                  QStringLiteral("修改活动监控设置（只传要改的字段，立即生效）。"
                                 "总开关涉及隐私，agent 应先复述再执行。"),
                  objectSchema(actSetProps));

    // ---- 收纳盒域 ----
    tools += tool(QStringLiteral("poptools_box_catalog_get"),
                  QStringLiteral("读取桌面收纳盒清单（名称/绑定目录/显隐）。"
                                 "用户说「帮我整理桌面文件夹」时先看现状再给方案。"),
                  objectSchema(QJsonObject{}));
    QJsonObject boxAddProps;
    boxAddProps.insert(QStringLiteral("name"), strProp(QStringLiteral("盒显示名")));
    boxAddProps.insert(QStringLiteral("dir"), strProp(QStringLiteral("绑定文件夹绝对路径；缺省=新建托管目录")));
    tools += tool(QStringLiteral("poptools_box_add"),
                  QStringLiteral("新建一个桌面收纳盒（可绑定既有文件夹）。"
                                 "桌面整理方案确定后用本工具落地。"),
                  objectSchema(boxAddProps));
    QJsonObject boxDelProps;
    boxDelProps.insert(QStringLiteral("id"), strProp(QStringLiteral("盒 id（catalog_get 取）")));
    tools += tool(QStringLiteral("poptools_box_remove"),
                  QStringLiteral("解除一个收纳盒绑定（绝不删除文件夹本身）。"),
                  objectSchema(boxDelProps));

    // ---- 翻译域 ----
    QJsonObject translateProps;
    translateProps.insert(QStringLiteral("text"), strProp(QStringLiteral("待翻译文本")));
    translateProps.insert(QStringLiteral("target"), strProp(QStringLiteral("目标语言代码，缺省=用户设置的目标语言")));
    tools += tool(QStringLiteral("poptools_translate_text"),
                  QStringLiteral("用 PopTools 配置的翻译 API 翻译文本（划词翻译同款管线）。"),
                  objectSchema(translateProps));

    // ---- 系统域 ----
    tools += tool(QStringLiteral("poptools_system_info"),
                  QStringLiteral("PopTools 自身信息（版本/数据目录/模块状态）——回答「PopTools 有什么功能/设置在哪」时用。"),
                  objectSchema(QJsonObject{}));
    QJsonObject sysGetProps;
    sysGetProps.insert(QStringLiteral("keys"), strProp(QStringLiteral("逗号分隔：autostart,pin_save_dir,hotkeys；缺省=全部")));
    tools += tool(QStringLiteral("poptools_system_settings_get"),
                  QStringLiteral("读取 PopTools 全局设置（开机启动/贴图保存目录/各功能热键）。"),
                  objectSchema(sysGetProps));
    QJsonObject sysSetProps;
    sysSetProps.insert(QStringLiteral("autostart"), boolProp(QStringLiteral("开机启动")));
    sysSetProps.insert(QStringLiteral("pin_save_dir"), strProp(QStringLiteral("贴图默认保存文件夹")));
    tools += tool(QStringLiteral("poptools_system_settings_set"),
                  QStringLiteral("修改 PopTools 全局设置（只传要改的字段，立即生效）。"),
                  objectSchema(sysSetProps));

    // ---- 上下文管理域（AI 自管理目录：千级日志先索引后取文） ----
    tools += tool(QStringLiteral("poptools_digest_list"),
                  QStringLiteral("列出全部日报/月报清单（名称/时间/标题）。"
                                 "用户问「我之前哪天写过什么/找一下某个事」时，先查本目录定位，"
                                 "再用 read_digest 按需读单篇——不要整批读入。"),
                  objectSchema(QJsonObject{}));
    QJsonObject searchProps;
    searchProps.insert(QStringLiteral("query"), strProp(QStringLiteral("关键词（逐文件包含匹配）")));
    searchProps.insert(QStringLiteral("scope"), strProp(QStringLiteral("daily/monthly/all，默认 all")));
    tools += tool(QStringLiteral("poptools_digest_search"),
                  QStringLiteral("全文搜索所有日报/月报（返回命中文件与行）。"
                                 "找事/回忆上下文时用：先 search 定位文件，再按需读取。"),
                  objectSchema(searchProps));

    return tools;
}

bool runDomainTool(const QString &monitorBaseDir, const QString &dataRoot,
                   const QString &name, const QJsonObject &args, McpToolResult *out)
{
    const QString goodyBaseDir = dataRoot + QStringLiteral("/goody");

    // ---- 日历域 ----
    if (name == QLatin1String("poptools_calendar_tasks_get")) {
        const QDate today = QDate::currentDate();
        const QString start = args.value(QStringLiteral("date_start")).toString();
        const QString end = args.value(QStringLiteral("date_end")).toString();
        const QDate from = start.isEmpty() ? today : QDate::fromString(start, QStringLiteral("yyyy-MM-dd"));
        QDate to = end.isEmpty() ? from : QDate::fromString(end, QStringLiteral("yyyy-MM-dd"));
        if (!from.isValid() || !to.isValid() || to < from) {
            *out = McpToolResult{err(QStringLiteral("日期非法或 end 早于 start")), true};
            return true;
        }
        if (from.daysTo(to) > 400)
            to = from.addDays(400); // 单次范围上限（上下文纪律）
        *out = {tasksJsonForRange(CalendarTaskStore::load(), from, to), false};
        return true;
    }
    if (name == QLatin1String("poptools_calendar_task_add")) {
        const QDate d = QDate::fromString(args.value(QStringLiteral("date")).toString(),
                                          QStringLiteral("yyyy-MM-dd"));
        const QString text = args.value(QStringLiteral("text")).toString().trimmed();
        if (!d.isValid() || text.isEmpty()) {
            *out = McpToolResult{err(QStringLiteral("date/text 必填且日期格式 YYYY-MM-DD")), true};
            return true;
        }
        CalendarTaskStore store = CalendarTaskStore::load();
        QVector<CalendarTaskItem> items = store.get(d);
        CalendarTaskItem item;
        item.text = text;
        item.time = args.value(QStringLiteral("time")).toString();
        items.append(item);
        store.set(d, items); // set 内部落盘
        touchDirty(dataRoot, QStringLiteral("calendar.dirty"));
        *out = {QJsonObject{{"ok", true},
                            {"note", QStringLiteral("日程已写入桌面日历 %1").arg(d.toString(QStringLiteral("yyyy-MM-dd")))}},
                false};
        return true;
    }
    if (name == QLatin1String("poptools_calendar_task_delete")) {
        const QDate d = QDate::fromString(args.value(QStringLiteral("date")).toString(),
                                          QStringLiteral("yyyy-MM-dd"));
        const QString match = args.value(QStringLiteral("match")).toString().trimmed();
        if (!d.isValid() || match.isEmpty()) {
            *out = McpToolResult{err(QStringLiteral("date/match 必填")), true};
            return true;
        }
        CalendarTaskStore store = CalendarTaskStore::load();
        QVector<CalendarTaskItem> items = store.get(d);
        bool removed = false;
        QVector<CalendarTaskItem> kept;
        for (const CalendarTaskItem &it : items) {
            if (!removed && it.text.contains(match)) {
                removed = true; // 只删第一条匹配
                continue;
            }
            kept.append(it);
        }
        if (!removed) {
            *out = McpToolResult{err(QStringLiteral("该日期没有匹配的日程")), true};
            return true;
        }
        store.set(d, kept);
        touchDirty(dataRoot, QStringLiteral("calendar.dirty"));
        *out = {QJsonObject{{"ok", true}, {"note", QStringLiteral("已删除日程：%1").arg(match)}},
                false};
        return true;
    }
    if (name == QLatin1String("poptools_calendar_settings_get")) {
        *out = {QJsonObject{{"ok", true},
                            {"settings", calendarSettingsJson(CalendarSettings::load())}},
                false};
        return true;
    }
    if (name == QLatin1String("poptools_calendar_settings_set")) {
        CalendarSettings cal = CalendarSettings::load();
        if (args.contains(QStringLiteral("show_lunar")))
            cal.showLunar = args.value(QStringLiteral("show_lunar")).toBool();
        if (args.contains(QStringLiteral("show_week_numbers")))
            cal.showWeekNumbers = args.value(QStringLiteral("show_week_numbers")).toBool();
        if (args.contains(QStringLiteral("show_events")))
            cal.showEvents = args.value(QStringLiteral("show_events")).toBool();
        if (args.contains(QStringLiteral("glass")))
            cal.glassEffect = args.value(QStringLiteral("glass")).toBool();
        if (args.contains(QStringLiteral("show_guide")))
            cal.showGuide = args.value(QStringLiteral("show_guide")).toBool();
        if (args.contains(QStringLiteral("show_list")))
            cal.hidden = !args.value(QStringLiteral("show_list")).toBool();
        if (args.contains(QStringLiteral("font_scale"))) {
            const int scale = args.value(QStringLiteral("font_scale")).toInt(100);
            cal.fontScale = qBound(70, scale, 160);
        }
        cal.save();
        touchDirty(dataRoot, QStringLiteral("calendar.dirty"));
        *out = {QJsonObject{{"ok", true},
                            {"settings", calendarSettingsJson(cal)},
                            {"note", QStringLiteral("桌面日历设置已生效")}},
                false};
        return true;
    }

    // ---- 活动监控域（Settings ini + settings.dirty → 主 app sync + 广播） ----
    if (name == QLatin1String("poptools_activity_settings_get")) {
        const Settings &s = Settings::instance();
        *out = {QJsonObject{{"ok", true},
                            {"enabled", s.monitorEnabled()},
                            {"ocr_snapshot", s.monitorOcrSnapshot()},
                            {"idle_sec", double(s.monitorIdleThresholdSec())},
                            {"retention_days", double(s.monitorRetentionDays())}},
                false};
        return true;
    }
    if (name == QLatin1String("poptools_activity_settings_set")) {
        Settings &s = Settings::instance();
        s.beginBatch();
        if (args.contains(QStringLiteral("enabled")))
            s.setMonitorEnabled(args.value(QStringLiteral("enabled")).toBool());
        if (args.contains(QStringLiteral("ocr_snapshot")))
            s.setMonitorOcrSnapshot(args.value(QStringLiteral("ocr_snapshot")).toBool());
        if (args.contains(QStringLiteral("idle_sec")))
            s.setMonitorIdleThresholdSec(qBound(10, args.value(QStringLiteral("idle_sec")).toInt(60), 3600));
        if (args.contains(QStringLiteral("retention_days")))
            s.setMonitorRetentionDays(qBound(1, args.value(QStringLiteral("retention_days")).toInt(30), 3650));
        s.endBatch();
        touchDirty(dataRoot, QStringLiteral("settings.dirty"));
        *out = {QJsonObject{{"ok", true}, {"note", QStringLiteral("活动监控设置已生效")}},
                false};
        return true;
    }

    // ---- 收纳盒域 ----
    if (name == QLatin1String("poptools_box_catalog_get")) {
        QJsonArray arr;
        for (const BoxDef &b : BoxCatalog::load().boxes())
            arr.append(QJsonObject{{"id", b.id},
                                   {"name", b.name},
                                   {"dir", b.dir},
                                   {"hidden", b.hidden},
                                   {"cols", double(b.cols)}});
        *out = McpToolResult{QJsonObject{{"ok", true}, {"boxes", arr}}, false};
        return true;
    }
    if (name == QLatin1String("poptools_box_add")) {
        const QString name2 = args.value(QStringLiteral("name")).toString().trimmed();
        if (name2.isEmpty()) {
            *out = McpToolResult{err(QStringLiteral("name 必填")), true};
            return true;
        }
        BoxDef def;
        def.name = name2;
        const QString dir = args.value(QStringLiteral("dir")).toString().trimmed();
        def.dir = dir.isEmpty() ? BoxCatalog::managedDirFor(name2) : dir;
        BoxCatalog catalog = BoxCatalog::load();
        const QString id = catalog.addBox(def); // addBox 内部落盘
        touchDirty(dataRoot, QStringLiteral("box.dirty"));
        *out = {QJsonObject{{"ok", true},
                            {"id", id},
                            {"dir", def.dir},
                            {"note", QStringLiteral("收纳盒已创建（盒内容随主程序实时刷新；"
                                                    "若面板未显示，一次设置变更或重启即出）")}},
                false};
        return true;
    }
    if (name == QLatin1String("poptools_box_remove")) {
        const QString id = args.value(QStringLiteral("id")).toString();
        BoxCatalog catalog = BoxCatalog::load();
        if (catalog.indexOf(id) < 0) {
            *out = McpToolResult{err(QStringLiteral("盒 id 不存在")), true};
            return true;
        }
        catalog.removeBox(id); // 仅解除绑定，绝不删文件夹
        catalog.save();
        touchDirty(dataRoot, QStringLiteral("box.dirty"));
        *out = {QJsonObject{{"ok", true},
                            {"note", QStringLiteral("已解除绑定（文件夹本身未动）")}},
                false};
        return true;
    }

    // ---- 系统域 ----
    if (name == QLatin1String("poptools_system_info")) {
        const Settings &s = Settings::instance();
        *out = {QJsonObject{{"ok", true},
                            {"app", QStringLiteral("PopTools 桌面工具箱")},
                            {"modules",
                             QJsonObject{{"capture", QStringLiteral("截图 F1")},
                                         {"pin", QStringLiteral("贴图 F3")},
                                         {"selection_translate", QStringLiteral("划词翻译")},
                                         {"activity_monitor", s.monitorEnabled() ? QStringLiteral("活动监控：开")
                                                                                 : QStringLiteral("活动监控：关")},
                                         {"goody", QStringLiteral("管家（本 dsh 界面）")},
                                         {"calendar", QStringLiteral("桌面日历")},
                                         {"box", QStringLiteral("收纳盒")}}},
                            {"data_dir", dataRoot},
                            {"note", QStringLiteral("各功能的设置都可以通过本工具集直接读写")}},
                false};
        return true;
    }
    if (name == QLatin1String("poptools_system_settings_get")) {
        const Settings &s = Settings::instance();
        const QString keys = args.value(QStringLiteral("keys")).toString();
        const bool all = keys.isEmpty();
        QJsonObject out2;
        if (all || keys.contains(QStringLiteral("autostart")))
            out2.insert(QStringLiteral("autostart"), s.autoStartEnabled());
        if (all || keys.contains(QStringLiteral("pin_save_dir")))
            out2.insert(QStringLiteral("pin_save_dir"), s.pinSaveDir());
        if (all || keys.contains(QStringLiteral("hotkeys")))
            out2.insert(QStringLiteral("hotkeys"),
                        QJsonObject{{"capture_vk", double(s.captureHotKeyVk())},
                                    {"pin_vk", double(s.selectionTranslateHotKeyVk())},
                                    {"goody_vk", double(s.assistantHotKeyVk())}});
        *out = McpToolResult{QJsonObject{{"ok", true}, {"settings", out2}}, false};
        return true;
    }
    if (name == QLatin1String("poptools_system_settings_set")) {
        Settings &s = Settings::instance();
        s.beginBatch();
        if (args.contains(QStringLiteral("autostart")))
            s.setAutoStartEnabled(args.value(QStringLiteral("autostart")).toBool());
        if (args.contains(QStringLiteral("pin_save_dir")))
            s.setPinSaveDir(args.value(QStringLiteral("pin_save_dir")).toString().trimmed());
        s.endBatch();
        touchDirty(dataRoot, QStringLiteral("settings.dirty"));
        *out = {QJsonObject{{"ok", true}, {"note", QStringLiteral("全局设置已生效")}},
                false};
        return true;
    }

    // ---- 翻译域（异步 Translator + 事件循环等待，同 OCR 模式） ----
    if (name == QLatin1String("poptools_translate_text")) {
        const QString text = args.value(QStringLiteral("text")).toString();
        if (text.trimmed().isEmpty()) {
            *out = McpToolResult{err(QStringLiteral("text 必填")), true};
            return true;
        }
        QString target = args.value(QStringLiteral("target")).toString().trimmed();
        if (target.isEmpty())
            target = Settings::instance().translateTargetLang();
        Translator translator;
        QString resultText, failure;
        bool ok = false;
        QEventLoop loop;
        QTimer::singleShot(60000, &loop, &QEventLoop::quit);
        QObject::connect(&translator, &Translator::translateFinished, &loop,
                         [&](bool okR, const QString &res, const QString &errText) {
                             ok = okR;
                             resultText = res;
                             failure = errText;
                             loop.quit();
                         });
        translator.translate(text, QStringLiteral("auto"), target, false);
        loop.exec();
        if (!ok) {
            *out = {QJsonObject{{"ok", false},
                                {"error", failure.isEmpty() ? QStringLiteral("翻译失败") : failure}},
                    true};
            return true;
        }
        *out = {QJsonObject{{"ok", true},
                            {"target", target},
                            {"result", resultText}},
                false};
        return true;
    }

    // ---- 上下文管理域 ----
    if (name == QLatin1String("poptools_digest_list")) {
        const QJsonArray items = digestInventory(goodyBaseDir);
        *out = {QJsonObject{{"ok", true},
                            {"count", double(items.size())},
                            {"items", items}},
                false};
        return true;
    }
    if (name == QLatin1String("poptools_digest_search")) {
        const QString query = args.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty()) {
            *out = McpToolResult{err(QStringLiteral("query 必填")), true};
            return true;
        }
        QString scope = args.value(QStringLiteral("scope")).toString(QStringLiteral("all"));
        if (scope != QLatin1String("daily") && scope != QLatin1String("monthly")
            && scope != QLatin1String("all"))
            scope = QStringLiteral("all");
        QJsonArray hits;
        int scanned = 0;
        const auto scanDir = [&](const QDir &d, const QString &kind) {
            for (const QFileInfo &fi : d.entryInfoList({"*.md"}, QDir::Files, QDir::Name)) {
                ++scanned;
                QFile f(fi.absoluteFilePath());
                if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;
                int lineNo = 0;
                QStringList matched;
                while (!f.atEnd() && matched.size() < 5) {
                    ++lineNo;
                    const QString line = QString::fromUtf8(f.readLine());
                    if (line.contains(query, Qt::CaseInsensitive))
                        matched << QStringLiteral("L%1: %2").arg(lineNo).arg(line.trimmed().left(160));
                }
                if (!matched.isEmpty())
                    hits.append(QJsonObject{{"kind", kind},
                                            {"name", fi.fileName()},
                                            {"matches", QJsonArray::fromStringList(matched)}});
                if (hits.size() >= 100) // 命中上限（上下文纪律）
                    return;
            }
        };
        if (scope == QLatin1String("all") || scope == QLatin1String("daily"))
            scanDir(QDir(goodyBaseDir + QStringLiteral("/daily")), QStringLiteral("daily"));
        if (scope == QLatin1String("all") || scope == QLatin1String("monthly"))
            scanDir(QDir(goodyBaseDir + QStringLiteral("/monthly")), QStringLiteral("monthly"));
        *out = {QJsonObject{{"ok", true},
                            {"query", query},
                            {"scanned_files", double(scanned)},
                            {"hits", hits}},
                false};
        return true;
    }

    return false; // 未命中 → 协议层回落核心工具
}

// 心跳触碰：tools/call 每次调用（ExternalSyncWatcher → 悬浮球呼吸）
void touchMcpHeartbeat(const QString &dataRoot)
{
    QFile f(dataRoot + QStringLiteral("/mcp.heartbeat"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QByteArray::number(QDateTime::currentMSecsSinceEpoch()));
}

} // namespace poptools
