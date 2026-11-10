#pragma once

#include <QtTest/QtTest>

#include <memory>
#include <vector>

#include "app/AppServices.h"
#include "app/FeatureRegistry.h"
#include "core/assistant/ChatSession.h"
#include "core/assistant/DailyAggregator.h"
#include "core/assistant/LlmClient.h"
#include "core/assistant/McpStdioClient.h"
#include "core/assistant/MemoryStore.h"
#include "core/assistant/PermissionManager.h"
#include "core/assistant/Scheduler.h"
#include "core/assistant/ToolRegistry.h"
#include "core/assistant/WorkingMemory.h"
#include "core/annotation/AnnotationCanvas.h"
#include "core/annotation/AnnotationItem.h"
#include "core/box/BoxStore.h"
#include "core/calendar/CalendarStore.h"
#include "core/calendar/LunarCalendar.h"
#include "core/capture/QrScanner.h"
#include "core/hotkey/HotKeyNames.h"
#include "core/monitor/ActivityStore.h"
#include "core/monitor/ImeCommitDetector.h"
#include "core/monitor/InputActivityMonitor.h"
#include "core/monitor/KeyboardTextComposer.h"
#include "core/monitor/SnapshotOcrPipeline.h"
#include "core/settings/Settings.h"
#include "core/translation/Languages.h"
#include "core/translation/Translator.h"
#include "ui/ThemeColors.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace poptools;

// ============================================================
// 域逻辑单元测试（无 GUI）：
//   * AnnotationCanvas：撤销/重做快照语义、交互式修改、橡皮擦
//   * Languages：显示名 ↔ 语言代码映射
//   * Translator 内联辅助：Base URL 规范化、API 格式与端点
//   * FeatureRegistry：注册顺序装配、按 id 查找
// ============================================================
class DomainTests : public QObject
{
    Q_OBJECT

private slots:
    // ---- AnnotationCanvas ----
    void canvas_add_undo_redo()
    {
        AnnotationCanvas c;
        QVERIFY(!c.canUndo());
        QVERIFY(!c.canRedo());

        c.addItem(AnnotationItem{}); // 入撤销栈
        QCOMPARE(c.items.size(), 1);
        QVERIFY(c.canUndo());
        QVERIFY(!c.canRedo()); // 新操作清空重做栈

        c.undo();
        QCOMPARE(c.items.size(), 0);
        QVERIFY(c.canRedo());

        c.redo();
        QCOMPARE(c.items.size(), 1);

        c.undo();
        c.undo(); // 多余撤销应安全无操作
        QCOMPARE(c.items.size(), 0);
    }

    void canvas_interactive_edit()
    {
        AnnotationCanvas c;
        c.addItem(AnnotationItem{});
        QCOMPARE(c.items.size(), 1);

        // 交互式修改：pushSnapshot 后原位替换，一次撤销即回修改前
        c.pushSnapshot();
        AnnotationItem moved{};
        moved.rect = QRectF(10, 10, 30, 30);
        c.setItem(0, moved);
        QCOMPARE(c.items.size(), 1);
        QCOMPARE(c.items[0].rect, QRectF(10, 10, 30, 30));
        c.undo();
        QCOMPARE(c.items.size(), 1);
        QVERIFY(c.items[0].rect.isNull());

        // setItem / removeItem 的越界安全
        c.setItem(5, moved);
        c.removeItem(-1);
        c.removeItem(5);
        QCOMPARE(c.items.size(), 1);
    }

    void canvas_clear_resets_undo()
    {
        AnnotationCanvas c;
        c.addItem(AnnotationItem{});
        c.addItem(AnnotationItem{});
        QVERIFY(c.canUndo());

        c.clear(); // 重新截图：标注与撤销/重做栈一并清空
        QCOMPARE(c.items.size(), 0);
        QVERIFY(!c.canUndo());
        QVERIFY(!c.canRedo());
    }

    // ---- Languages ----
    void languages_round_trip()
    {
        const QStringList displays = languageDisplays();
        QVERIFY(displays.size() >= 9);
        for (const QString &display : displays)
            QCOMPARE(langCodeToDisplay(displayToLangCode(display)), display);
    }

    void languages_unknown_passthrough()
    {
        // 无法识别的显示名原样返回（兼容自定义语言代码）
        QCOMPARE(displayToLangCode(QStringLiteral(" pt ")), QStringLiteral("pt"));
        QCOMPARE(langCodeToDisplay(QStringLiteral("pt")), QStringLiteral("pt"));
    }

    // ---- HotKeyNames ----
    void hotkey_display_names()
    {
        QCOMPARE(hotKeyDisplayName(0x70), QStringLiteral("F1"));
        QCOMPARE(hotKeyDisplayName(0x87), QStringLiteral("F24"));
        QCOMPARE(hotKeyDisplayName(0x58), QStringLiteral("X"));
        QCOMPARE(hotKeyDisplayName(0x31), QStringLiteral("1"));
        // 修饰键顺序固定 Ctrl+Alt+Shift+Win（与划词热键文案一致）
        QCOMPARE(hotKeyDisplayName(0x58, 0x2 | 0x1 | 0x4),
                 QStringLiteral("Ctrl+Alt+Shift+X"));
        QCOMPARE(hotKeyDisplayName(0x21, 0x4), QStringLiteral("Shift+PageUp"));
        QCOMPARE(hotKeyDisplayName(0x61), QStringLiteral("Num1"));
        // 未知键回退十六进制码
        QCOMPARE(hotKeyDisplayName(0xAB), QStringLiteral("0xAB"));
    }

    // ---- ThemeColors ----
    void theme_colors_light_and_dark()
    {
        const ThemeColors light = uiThemeColors(false);
        const ThemeColors dark = uiThemeColors(true);
        QVERIFY(!light.dark);
        QVERIFY(dark.dark);
        // 浅色=纯黑文字白底；深色=纯白文字黑底（用户约定：不允许灰字）
        QCOMPARE(QByteArray(light.cardBg), QByteArray("#FFFFFF"));
        QCOMPARE(QByteArray(light.body), QByteArray("#000000"));
        QCOMPARE(QByteArray(light.sectionTitle), QByteArray("#000000"));
        QCOMPARE(QByteArray(light.title), QByteArray("#000000"));
        QCOMPARE(QByteArray(dark.cardBg), QByteArray("#1E1E1E"));
        QCOMPARE(QByteArray(dark.body), QByteArray("#FFFFFF"));
        QCOMPARE(QByteArray(dark.sectionTitle), QByteArray("#FFFFFF"));
        QCOMPARE(QByteArray(dark.title), QByteArray("#FFFFFF"));
        // 禁用态按钮文字同样不许灰（本应用无输入提示语义）
        QCOMPARE(QByteArray(light.ctrlDisabledText), QByteArray("#000000"));
        QCOMPARE(QByteArray(dark.ctrlDisabledText), QByteArray("#FFFFFF"));
        // 灰色只允许出现在线框：卡片边框 / 文本区边框 / 分隔线
        QVERIFY(light.line && *light.line);
        QVERIFY(dark.line && *dark.line);
        QVERIFY(light.cardBorder && *light.cardBorder);
        QVERIFY(dark.cardBorder && *dark.cardBorder);
        QVERIFY(light.editBorder && *light.editBorder);
        QVERIFY(dark.editBorder && *dark.editBorder);
        // 错误态三色齐全
        QVERIFY(light.errText && light.errBg && light.errBorder);
        QVERIFY(dark.errText && dark.errBg && dark.errBorder);
    }

    // ---- Translator 内联辅助 ----
    void api_base_url_normalize()
    {
        QCOMPARE(normalizeApiBaseUrl(QStringLiteral(" https://api.x.com/v1/ ")),
                 QStringLiteral("https://api.x.com/v1"));
        QCOMPARE(normalizeApiBaseUrl(QStringLiteral("https://h///")),
                 QStringLiteral("https://h"));
        QCOMPARE(normalizeApiBaseUrl(QStringLiteral("  ")), QString());
    }

    void api_format_and_endpoint()
    {
        // 设置存储值 → 枚举，未知值回退 OpenAI
        QCOMPARE(apiFormatFromSetting(QStringLiteral("anthropic")), ApiFormat::Anthropic);
        QCOMPARE(apiFormatFromSetting(QStringLiteral("openai")), ApiFormat::OpenAI);
        QCOMPARE(apiFormatFromSetting(QString()), ApiFormat::OpenAI);

        const QString base = QStringLiteral("https://api.x.com");
        QCOMPARE(apiEndpointPath(ApiFormat::OpenAI, base),
                 QStringLiteral("/chat/completions"));
        // Anthropic：兼容服务常挂 …/anthropic 前缀
        QCOMPARE(apiEndpointPath(ApiFormat::Anthropic,
                                 QStringLiteral("https://api.deepseek.com/anthropic")),
                 QStringLiteral("/v1/messages"));
        // base 已以 /v1 结尾时不重复追加
        QCOMPARE(apiEndpointPath(ApiFormat::Anthropic, QStringLiteral("https://x.com/v1")),
                 QStringLiteral("/messages"));
    }

    // ---- FeatureRegistry ----
    void registry_init_order_and_lookup()
    {
        std::vector<QString> order;
        struct RecModule : IFeatureModule
        {
            RecModule(const char *id, std::vector<QString> *order)
                : m_id(id), m_order(order) {}
            QString id() const override { return QLatin1String(m_id); }
            void init(AppServices &) override { m_order->push_back(m_id); }
            const char *m_id;
            std::vector<QString> *m_order;
        };

        FeatureRegistry registry;
        registry.add(std::make_unique<RecModule>("b", &order));
        registry.add(std::make_unique<RecModule>("a", &order));

        AppServices services;
        registry.initAll(services);
        // 按注册顺序装配
        QCOMPARE(order.size(), size_t(2));
        QCOMPARE(order[0], QStringLiteral("b"));
        QCOMPARE(order[1], QStringLiteral("a"));

        // 按 id 查找
        QVERIFY(registry.module(QStringLiteral("a")) != nullptr);
        QCOMPARE(registry.module(QStringLiteral("a"))->id(), QStringLiteral("a"));
        QVERIFY(registry.module(QStringLiteral("missing")) == nullptr);

        registry.shutdownAll(); // 应安全无操作（Fake 未覆写 shutdown）
    }

    // ---- Translator 请求体构造（双协议纯函数，单一实现源） ----
    void translator_build_request_body()
    {
        // OpenAI：messages = [system, user]，thinking 字段随 disableThinking 取舍
        const QJsonObject o = QJsonDocument::fromJson(
            buildTranslateRequestBody(ApiFormat::OpenAI, "m1", "SYS", "TEXT", true)).object();
        QCOMPARE(o.value("model").toString(), QStringLiteral("m1"));
        QCOMPARE(o.value("messages").toArray().size(), 2);
        QCOMPARE(o.value("messages").toArray()[0].toObject().value("role").toString(),
                 QStringLiteral("system"));
        QCOMPARE(o.value("messages").toArray()[1].toObject().value("content").toString(),
                 QStringLiteral("TEXT"));
        QVERIFY(o.contains("thinking"));

        const QJsonObject o2 = QJsonDocument::fromJson(
            buildTranslateRequestBody(ApiFormat::OpenAI, "m1", "SYS", "T", false)).object();
        QVERIFY(!o2.contains("thinking"));

        // Anthropic：system 独立于 messages、max_tokens 必填、messages 只有 user
        const QJsonObject a = QJsonDocument::fromJson(
            buildTranslateRequestBody(ApiFormat::Anthropic, "m2", "SYS", "TEXT", true)).object();
        QCOMPARE(a.value("system").toString(), QStringLiteral("SYS"));
        QCOMPARE(a.value("max_tokens").toInt(), 8192);
        QCOMPARE(a.value("messages").toArray().size(), 1);
        QVERIFY(a.contains("thinking"));

        const QJsonObject a2 = QJsonDocument::fromJson(
            buildTranslateRequestBody(ApiFormat::Anthropic, "m2", "SYS", "T", false)).object();
        QVERIFY(!a2.contains("thinking"));
    }

    // ---- Translator 动作提示词（翻译 / 解释 / 总结 / 改写） ----
    void translator_action_prompts()
    {
        // 翻译（OCR 来源）：保留 from→to 指令与 OCR 容错说明
        const QString tr = translateActionPrompt(LlmAction::Translate, "en", "zh", true);
        QVERIFY(tr.contains("Translate"));
        QVERIFY(tr.contains("OCR"));
        QVERIFY(tr.contains("Chinese"));
        // 解释 / 总结：输出语言 = 目标语言
        QVERIFY(translateActionPrompt(LlmAction::Explain, "auto", "zh", false).contains("Chinese"));
        QVERIFY(translateActionPrompt(LlmAction::Summarize, "auto", "en", false).contains("English"));
        // 改写：保持原文语言
        QVERIFY(translateActionPrompt(LlmAction::Rewrite, "en", "zh", false)
                    .contains("original language"));
        // 动作键往返 + 未知键回退翻译 + 下拉项数量
        QCOMPARE(llmActionKey(llmActionFromKey(QStringLiteral("rewrite"))),
                 QStringLiteral("rewrite"));
        QVERIFY(llmActionFromKey(QStringLiteral("bogus")) == LlmAction::Translate);
        QCOMPARE(llmActionNames().size(), 4);
    }

    // ---- 二维码识别（zxing-cpp 后端；夹具图内含一个已知内容的二维码） ----
    void qr_scan_fixture()
    {
        if (!qr::available())
            QSKIP("zxing-cpp 未安装（tools/zxing 缺失），跳过");
        QImage img(QStringLiteral("fixtures/qr_test.png"));
        QVERIFY(!img.isNull());
        const QStringList codes = qr::scan(img);
        QCOMPARE(codes.size(), 1);
        QCOMPARE(codes.first(), QStringLiteral("https://poptools.example/qr-test"));
        // 无码图返回空
        QImage blank(64, 64, QImage::Format_ARGB32);
        blank.fill(Qt::white);
        QVERIFY(qr::scan(blank).isEmpty());
    }

    // ---- Settings：注入式实例读写与非法值回退 ----
    void settings_injected_roundtrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Settings s(dir.filePath("test.ini"));

        s.setCaptureHotKeyVk(0x71); // F2
        QCOMPARE(s.captureHotKeyVk(), quint32(0x71));
        // 非法值（0 / 越界）读取时回退默认 F1——否则 vk=0 会传进 RegisterHotKey
        s.setCaptureHotKeyVk(0);
        QCOMPARE(s.captureHotKeyVk(), quint32(0x70));
        s.setCaptureHotKeyVk(0xFFFF);
        QCOMPARE(s.captureHotKeyVk(), quint32(0x70));

        // 划词热键同规则
        s.setSelectionTranslateHotKey(0x59 /* Y */, 0x0007);
        QCOMPARE(s.selectionTranslateHotKeyVk(), quint32(0x59));
        s.setSelectionTranslateHotKey(0, 0x0007);
        QCOMPARE(s.selectionTranslateHotKeyVk(), quint32(0x58));

        // 持久化：同路径重新打开读到已保存值
        s.setApiBaseUrl(QStringLiteral("https://example.com/v1"));
        Settings s2(dir.filePath("test.ini"));
        QCOMPARE(s2.apiBaseUrl(), QStringLiteral("https://example.com/v1"));
    }

    // ---- 桌面日历：农历换算（已知锚点日期） ----
    void lunar_known_dates()
    {
        // 春节锚点（正月初一）：2024/2025/2026 三年的公历日期为公开事实
        const LunarDate cny2024 = lunar::fromSolar(QDate(2024, 2, 10));
        QVERIFY(cny2024.isValid());
        QCOMPARE(cny2024.year, 2024);
        QCOMPARE(cny2024.month, 1);
        QCOMPARE(cny2024.day, 1);
        QVERIFY(!cny2024.leap);

        const LunarDate cny2025 = lunar::fromSolar(QDate(2025, 1, 29));
        QCOMPARE(cny2025.year, 2025);
        QCOMPARE(cny2025.month, 1);

        const LunarDate cny2026 = lunar::fromSolar(QDate(2026, 2, 17));
        QCOMPARE(cny2026.year, 2026);
        QCOMPARE(cny2026.month, 1);
        QCOMPARE(cny2026.day, 1);

        // 中秋：2025-10-06 与 2026-09-25 均为八月十五
        const LunarDate midAutumn2025 = lunar::fromSolar(QDate(2025, 10, 6));
        QCOMPARE(midAutumn2025.month, 8);
        QCOMPARE(midAutumn2025.day, 15);
        const LunarDate midAutumn2026 = lunar::fromSolar(QDate(2026, 9, 25));
        QCOMPARE(midAutumn2026.month, 8);
        QCOMPARE(midAutumn2026.day, 15);

        // 闰月：2023 年闰二月，3 月 22 日为闰二月初一（二月为 30 天大月）
        const LunarDate leap = lunar::fromSolar(QDate(2023, 3, 22));
        QCOMPARE(leap.year, 2023);
        QCOMPARE(leap.month, 2);
        QCOMPARE(leap.day, 1);
        QVERIFY(leap.leap);
        QCOMPARE(leap.monthName(), QStringLiteral("闰二月"));

        // 月名/日名
        QCOMPARE(cny2026.monthName(), QStringLiteral("正月"));
        QCOMPARE(cny2026.dayName(), QStringLiteral("初一"));
        QCOMPARE(lunar::fromSolar(QDate(2026, 2, 26)).dayName(), QStringLiteral("初十"));
        QCOMPARE(lunar::fromSolar(QDate(2026, 2, 28)).dayName(), QStringLiteral("十二"));

        // 范围护栏
        QVERIFY(!lunar::fromSolar(QDate(1899, 1, 1)).isValid());
        QVERIFY(!lunar::fromSolar(QDate(2110, 1, 1)).isValid());
    }

    // ---- 桌面日历：月历格小字（节日 / 朔日月名 / 平日日名） ----
    void lunar_day_label()
    {
        bool festival = false;
        QCOMPARE(lunar::dayLabel(QDate(2026, 2, 17), &festival), QStringLiteral("春节"));
        QVERIFY(festival);
        QCOMPARE(lunar::dayLabel(QDate(2026, 9, 25), &festival), QStringLiteral("中秋节"));
        QVERIFY(festival);
        QCOMPARE(lunar::dayLabel(QDate(2026, 10, 1), &festival), QStringLiteral("国庆节"));
        QVERIFY(festival);

        // 除夕 = 春节前一天
        QCOMPARE(lunar::dayLabel(QDate(2026, 2, 16), &festival), QStringLiteral("除夕"));
        QVERIFY(festival);

        // 初一显示月名（非节日）；平日显示日名
        QCOMPARE(lunar::dayLabel(QDate(2026, 3, 19), &festival), QStringLiteral("二月"));
        QVERIFY(!festival);
        QCOMPARE(lunar::dayLabel(QDate(2026, 3, 21), &festival), QStringLiteral("初三"));
        QVERIFY(!festival);
    }

    // ---- 桌面日历：CalendarStore 增删改/排序/分类 JSON 往返（T-043 合并列表语义） ----
    void calendar_store_roundtrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("calendar.json");

        CalendarStore store(path);
        store.load();
        // 播种默认分类
        QVERIFY(store.categories().size() >= 2);
        const QString workId = [&] {
            for (const CalendarCategory &c : store.categories())
                if (c.name == QStringLiteral("工作"))
                    return c.id;
            return QString();
        }();
        QVERIFY(!workId.isEmpty());

        const QDate day(2026, 11, 8);
        const QString item1 = store.addItem(day, workId, QStringLiteral("写周报"), true);
        const QString item2 = store.addItem(day, workId, QStringLiteral("买咖啡"), false);
        QVERIFY(!item1.isEmpty() && !item2.isEmpty());
        QCOMPARE(store.itemsForDate(day).size(), 2);

        // 完成切换
        QVERIFY(store.setDone(item1, true));
        QVERIFY(!store.setDone(item1, true)); // 幂等
        QCOMPARE(store.itemsForDate(day).at(0).done, true);

        // 上移/下移：moveItem(-1) 把 item2 提到 item1 前
        QVERIFY(store.moveItem(item2, -1));
        QCOMPARE(store.itemsForDate(day).at(0).id, item2);
        QVERIFY(!store.moveItem(item2, -1)); // 已在顶
        QVERIFY(store.moveItem(item2, 1));
        QCOMPARE(store.itemsForDate(day).at(0).id, item1);

        // 一键安排到其他日期
        QVERIFY(store.moveItemToDate(item2, day.addDays(1)));
        QCOMPARE(store.itemsForDate(day).size(), 1);
        QCOMPARE(store.itemsForDate(day.addDays(1)).size(), 1);

        // 拖拽重排回写（先补第二项，取到 2 项再做重排）
        store.addItem(day, workId, QStringLiteral("第三项"), false);
        const QList<CalendarItem> dayItems = store.itemsForDate(day);
        QVERIFY(dayItems.size() == 2);
        QStringList newOrder{ dayItems.at(1).id, dayItems.at(0).id };
        store.reorderItems(day, newOrder);
        QCOMPARE(store.itemsForDate(day).at(0).id, newOrder.at(0));
        QCOMPARE(store.itemsForDate(day).at(1).id, newOrder.at(1));

        // 分类增改删（删除后条目改挂默认分类）
        const QString tempCat = store.addCategory(QStringLiteral("临时"), QColor("#123456"));
        QVERIFY(!tempCat.isEmpty());
        QVERIFY(store.updateCategory(tempCat, QStringLiteral("临时改"), QColor("#654321")));
        store.addItem(day, tempCat, QStringLiteral("带临时分类"), false);
        QVERIFY(store.removeCategory(tempCat));
        QVERIFY(!store.removeCategory(tempCat)); // 再删无效
        QVERIFY(!store.removeCategory(CalendarStore::defaultCategoryId())); // 默认分类不可删
        for (const CalendarItem &it : store.itemsForDate(day))
            QVERIFY(it.categoryId != tempCat);

        // JSON 往返：重新加载同一路径，条目与属性一致
        const QList<CalendarItem> before = store.items();
        const QList<CalendarCategory> catsBefore = store.categories();
        CalendarStore reloaded(path);
        reloaded.load();
        QCOMPARE(reloaded.items().size(), before.size());
        QCOMPARE(reloaded.categories().size(), catsBefore.size());
        QCOMPARE(reloaded.itemsForDate(day).size(), 3);
        QCOMPARE(reloaded.itemsForDate(day.addDays(1)).size(), 1);
        // 完成态与重要标记均持久化
        for (const CalendarItem &it : reloaded.itemsForDate(day)) {
            if (it.id == item1)
                QVERIFY(it.done);
            if (it.id == dayItems.at(0).id)
                QCOMPARE(it.important, true);
        }
    }

    // ---- BoxStore：盒子增删改 + 位置持久化 + JSON 往返 ----
    void box_store_roundtrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("boxes.json");
        BoxStore store(path);
        store.load();
        QVERIFY(store.boxes().isEmpty());

        const QString id1 = store.addBox(QStringLiteral("学习"), dir.filePath("学习"), 60, 120);
        const QString id2 = store.addBox(QStringLiteral("学习"), QString(), 320, 120); // 重名自动加序号
        QVERIFY(!id1.isEmpty() && !id2.isEmpty());
        QCOMPARE(store.boxes().size(), 2);
        QCOMPARE(store.boxes().at(1).name, QStringLiteral("学习 2"));

        bool changed = false;
        QMetaObject::Connection conn =
            connect(&store, &BoxStore::changed, [&changed] { changed = true; });
        QVERIFY(store.setBoxPos(id1, 88, 220));
        QVERIFY(changed);
        disconnect(conn);

        // 位置与目录持久化（JSON 往返）
        BoxStore reloaded(path);
        reloaded.load();
        QCOMPARE(reloaded.boxes().size(), 2);
        QCOMPARE(reloaded.boxes().first().x, 88);
        QCOMPARE(reloaded.boxes().first().y, 220);
        QVERIFY(QFileInfo::exists(reloaded.boxes().first().dir)); // 目录自动创建
        QVERIFY(reloaded.boxes().first().dir.endsWith(QStringLiteral("学习")));

        // 改名 / 删除（目录不动）
        QVERIFY(reloaded.renameBox(id1, QStringLiteral("学习资料")));
        QCOMPARE(reloaded.boxes().first().name, QStringLiteral("学习资料"));
        const QString dir1 = reloaded.boxes().first().dir;
        QVERIFY(reloaded.removeBox(id1));
        QVERIFY(!reloaded.removeBox(id1));
        QVERIFY(QFileInfo::exists(dir1)); // 删盒子不删目录
    }

    // ---- BoxStore::moveUrlsIntoDir：同名去重与移动 ----
    void box_move_urls_into_dir()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString src = dir.filePath("src");
        const QString dst = dir.filePath("dst");
        QVERIFY(QDir().mkpath(src) && QDir().mkpath(dst));
        QFile f1(src + "/a.txt"), f2(src + "/b.txt");
        QVERIFY(f1.open(QIODevice::WriteOnly) && f1.write("1") > 0);
        QVERIFY(f2.open(QIODevice::WriteOnly) && f2.write("2") > 0);
        QFile dup(dst + "/a.txt");
        QVERIFY(dup.open(QIODevice::WriteOnly) && dup.write("dup") > 0);
        // Windows 下句柄未关的文件不能 rename（共享冲突），先落盘关闭
        f1.close();
        f2.close();
        dup.close();

        QList<QUrl> urls;
        urls << QUrl::fromLocalFile(f1.fileName()) << QUrl::fromLocalFile(f2.fileName());
        const QStringList failures = BoxStore::moveUrlsIntoDir(dst, urls);
        QVERIFY(failures.isEmpty());
        QVERIFY(!QFileInfo::exists(f1.fileName()));
        QFile check(dst + "/a.txt"); // 原同名文件未覆盖（内容仍是 dup）
        QVERIFY(check.open(QIODevice::ReadOnly));
        QCOMPARE(check.readAll(), QByteArray("dup"));
        QVERIFY(QFileInfo::exists(dst + "/a (2).txt")); // 新来的加了序号
        QVERIFY(QFileInfo::exists(dst + "/b.txt"));
    }

    // ---- ActivityStore：JSONL 落盘 / meta 首建 / 保留期清理 ----
    void activity_store_write_and_prune()
    {
        QTemporaryDir base;
        QVERIFY(base.isValid());

        {
            ActivityStore store(base.path());
            store.setRetentionDays(1);
            store.start();
            QJsonObject ev;
            ev.insert("ts", 1000);
            ev.insert("text", QStringLiteral("hello"));
            store.append(QStringLiteral("keys"), ev);
            store.flushBlocking(); // 同步等待 worker 落盘

            const QString dayDir =
                base.path() + QLatin1Char('/') + QDate::currentDate().toString("yyyy-MM-dd");
            QFile keys(dayDir + "/keys.jsonl");
            QVERIFY(keys.open(QIODevice::ReadOnly));
            const QByteArray firstLine = keys.readAll().split('\n').first();
            QCOMPARE(QJsonDocument::fromJson(firstLine).object().value("text").toString(),
                     QStringLiteral("hello"));
            keys.close();
            QVERIFY(QFileInfo::exists(dayDir + "/meta.json")); // 日目录首建自带 meta

            // 第二个流、第二批写入（句柄复用路径）
            QJsonObject seg;
            seg.insert("start", 0);
            store.append(QStringLiteral("timeline"), seg);
            store.flushBlocking();
            QVERIFY(QFileInfo::exists(dayDir + "/timeline.jsonl"));
        }

        // 保留期：手工造一个 3 天前的日目录，retention=1 的 store 启动即清
        const QString oldDay = base.path() + QLatin1Char('/')
                               + QDate::currentDate().addDays(-3).toString("yyyy-MM-dd");
        QVERIFY(QDir().mkpath(oldDay));
        QFile oldFile(oldDay + "/keys.jsonl");
        QVERIFY(oldFile.open(QIODevice::WriteOnly));
        oldFile.close();
        ActivityStore store2(base.path());
        store2.setRetentionDays(1);
        store2.start(); // prune 排入 worker 队列
        store2.stop();  // quit 事件排在 prune 之后 → wait() 返回时清理已完成
        QVERIFY(!QDir(oldDay).exists());
    }

    // ---- KeyboardTextComposer：文本组装 / 退格 / 打码 / 快捷键不记 ----
    void keyboard_composer_text_and_masking()
    {
        // Win32 虚拟键码（与 <windows.h> 同值；测试进程不引系统头）
        constexpr int kVkBack = 0x08, kVkReturn = 0x0D, kVkControl = 0x11;

        KeyboardTextComposer c;
        QString gotApp, gotTitle, gotText;
        bool gotMasked = false;
        connect(&c, &KeyboardTextComposer::textReady,
                [&](const QString &a, const QString &t, const QString &x, bool m) {
                    gotApp = a;
                    gotTitle = t;
                    gotText = x;
                    gotMasked = m;
                });
        c.setWindowContext(QStringLiteral("notepad.exe"), QStringLiteral("无标题"));

        auto key = [&c](int vk, bool down = true) { c.onKeyEvent(vk, 0, false, down); };

        // 小写键入 + 空闲冲刷（VK_A 在常见拉丁布局下 → 'a'）
        key(0x41); // A
        key(0x54); // T
        c.onIdleTick(QDateTime::currentMSecsSinceEpoch() + 10000);
        QCOMPARE(gotText, QStringLiteral("at"));
        QCOMPARE(gotApp, QStringLiteral("notepad.exe"));
        QVERIFY(!gotMasked);

        // 退格从缓冲删字
        gotText.clear();
        c.setWindowContext(QStringLiteral("a.exe"), QStringLiteral("t")); // 换窗冲刷（空缓冲无信号）
        key(0x41);
        key(0x42);
        key(kVkBack);
        c.flush();
        QCOMPARE(gotText, QStringLiteral("a"));

        // 回车：追加换行并立即冲刷
        gotText.clear();
        key(0x41);
        key(kVkReturn);
        QCOMPARE(gotText, QStringLiteral("a\n"));

        // 密码打码：等长占位符 + masked 标记
        gotText.clear();
        c.setMasked(true);
        key(0x41);
        key(0x41);
        c.flush();
        QCOMPARE(gotText, QString(QChar(0x25CF)) + QChar(0x25CF));
        QVERIFY(gotMasked);
        c.setMasked(false);

        // Ctrl 组合键（快捷键）不记录
        gotText.clear();
        key(kVkControl);
        key(0x43);
        key(kVkControl, false);
        c.flush();
        QVERIFY(gotText.isEmpty());
    }

    // ---- InputActivityMonitor：时间线状态机 ----
    void input_monitor_segments()
    {
        InputActivityMonitor m;
        m.setIdleThresholdSec(120);
        qint64 clock = 1000000;
        m.setClock([&clock] { return clock; });

        QList<TimelineSegment> closed;
        connect(&m, &InputActivityMonitor::segmentClosed,
                [&closed](const TimelineSegment &s) { closed << s; });

        m.setWindowContext(1, QStringLiteral("a.exe"), QStringLiteral("Doc1"));
        m.noteKeydown();
        m.noteClick();
        for (int i = 0; i < 10; ++i) {
            clock += 1000;
            m.tick(); // 10 秒活跃
        }
        QCOMPARE(closed.size(), 0);

        // 空闲超时 → 段闭合，end = 最后输入时刻（发呆不计时长）
        clock += 200 * 1000;
        m.tick();
        QCOMPARE(closed.size(), 1);
        QVERIFY(closed.last().idleEnded);
        QCOMPARE(closed.last().app, QStringLiteral("a.exe"));
        QCOMPARE(closed.last().keystrokes, 1);
        QCOMPARE(closed.last().clicks, 1);
        QCOMPARE(closed.last().activeSec, 10);
        QVERIFY(closed.last().endMs < clock - 100 * 1000);

        // 空闲后同窗输入 → 原位续开新段（无需前台事件）
        m.noteKeydown();
        m.noteKeydown();
        clock += 1000;
        m.tick();
        m.flush();
        QCOMPARE(closed.size(), 2);
        QCOMPARE(closed.last().keystrokes, 2);
        QVERIFY(!closed.last().idleEnded);

        // 切窗 → 旧段闭合；新窗无输入不开段
        clock += 1000;
        m.setWindowContext(2, QStringLiteral("b.exe"), QStringLiteral("Other"));
        m.noteClick();
        clock += 1000;
        m.setWindowContext(3, QStringLiteral("c.exe"), QStringLiteral("Never"));
        m.flush();
        QCOMPARE(closed.size(), 3);
        QCOMPARE(closed.last().app, QStringLiteral("b.exe"));
        QCOMPARE(closed.last().clicks, 1);

        // 同窗长会话 60s 切片：满 60s 自动闭合并原位续开
        m.setWindowContext(4, QStringLiteral("d.exe"), QStringLiteral("Long"));
        m.noteKeydown();
        for (int i = 0; i < 60; ++i) {
            clock += 1000;
            m.tick();
        }
        QCOMPARE(closed.size(), 4); // 第 60s 心跳触发切片
        QCOMPARE(closed.last().activeSec, 60);
        m.flush();
        QCOMPARE(closed.size(), 5); // 切片后的续段
        QCOMPARE(closed.last().activeSec, 0);
    }

    // ---- Settings：monitor/* 键读写与钳制 ----
    void settings_monitor_keys()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath("settings_monitor.ini");
        {
            Settings s(ini);
            QVERIFY(!s.monitorEnabled()); // 默认关闭
            s.setMonitorEnabled(true);
            s.setMonitorMaskPasswords(false);
            s.setMonitorIdleThresholdSec(5);  // 低于下限 → 钳到 10
            s.setMonitorRetentionDays(99999); // 超上限 → 钳到 3650
            s.setMonitorOcrSnapshot(true);
        }
        Settings s(ini);
        QVERIFY(s.monitorEnabled());
        QVERIFY(!s.monitorMaskPasswords());
        QCOMPARE(s.monitorIdleThresholdSec(), 10);
        QCOMPARE(s.monitorRetentionDays(), 3650);
        QVERIFY(s.monitorOcrSnapshot());
    }

    // ---- ImeCommitDetector：CJK 布局 + 字母后确认键 = 疑似上屏 ----
    void ime_commit_detector()
    {
        ImeCommitDetector d;
        qint64 clock = 1000000;
        d.setClock([&clock] { return clock; });
        bool cjkIme = true;
        d.setImeGate([&cjkIme] { return cjkIme; });

        int commits = 0;
        connect(&d, &ImeCommitDetector::commitDetected, [&] { ++commits; });

        const int kVkA = 0x41, kVkSpace = 0x20, kVkReturn = 0x0D, kVkD2 = 0x32;

        // 英文布局：字母 + 空格 → 不触发
        cjkIme = false;
        d.onKeyEvent(kVkA, true);
        d.onKeyEvent(kVkSpace, true);
        QCOMPARE(commits, 0);

        // CJK 布局：拼音字母后按候选确认键 → 触发（数字备选与空格首选各一次）
        cjkIme = true;
        d.onKeyEvent(kVkA, true);
        d.onKeyEvent(kVkD2, true); // 数字 2 = 候选 2 确认
        QCOMPARE(commits, 1);
        d.onKeyEvent(kVkSpace, true); // 同一组合期内的空格首选：再次触发
        QCOMPARE(commits, 2);

        // 超过 3s 的字母记忆失效：老字母 + 空格 → 不触发
        clock += 10000;
        d.onKeyEvent(kVkSpace, true);
        QCOMPARE(commits, 2);

        // 换窗重置
        d.onKeyEvent(kVkA, true);
        d.reset();
        d.onKeyEvent(kVkSpace, true);
        QCOMPARE(commits, 2);

        // CJK 布局下字母 + 回车（上屏原文）→ 触发
        d.onKeyEvent(kVkA, true);
        d.onKeyEvent(kVkReturn, true);
        QCOMPARE(commits, 3);
    }

    // ---- SnapshotOcrPipeline::diffAdded：增量去重 ----
    void ocr_diff_added()
    {
        // 首次：全量记入
        QCOMPARE(SnapshotOcrPipeline::diffAdded(QString(), QStringLiteral("你好世界")),
                 QStringLiteral("你好世界"));
        // 打字增长型：新增在尾部
        QCOMPARE(SnapshotOcrPipeline::diffAdded(QStringLiteral("你好"), QStringLiteral("你好世界")),
                 QStringLiteral("世界"));
        // 屏幕滚动型：新增在头部
        QCOMPARE(SnapshotOcrPipeline::diffAdded(QStringLiteral("世界"), QStringLiteral("你好世界")),
                 QStringLiteral("你好"));
        // 中段插入型（endsWith 分支命中，尾部空格由调用方 trim）
        QCOMPARE(SnapshotOcrPipeline::diffAdded(QStringLiteral("B C"), QStringLiteral("A B C")),
                 QStringLiteral("A "));
        // 版面大改：宁缺毋滥
        QVERIFY(SnapshotOcrPipeline::diffAdded(QStringLiteral("旧版面"), QStringLiteral("完全不同"))
                    .isEmpty());
        // 无变化
        QVERIFY(SnapshotOcrPipeline::diffAdded(QStringLiteral("同"), QStringLiteral("同")).isEmpty());
    }

    // ---- LlmStreamParser：SSE 分块解析 + 工具调用聚合 ----
    void llm_stream_parser()
    {
        LlmStreamParser p;
        // 故意切在不整行处喂入（半行残留路径）
        const QByteArray chunk1 =
            "data: {\"choices\":[{\"delta\":{\"content\":\"你好\"}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"con";
        const QByteArray chunk2 =
            "tent\":\"，世界\"}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_1\","
            "\"function\":{\"name\":\"shell.run\",\"arguments\":\"{\\\"command\\\":\\\"git st\"}}]}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"function\":{\"arguments\":\"atus\\\"}\"}}]},\"finish_reason\":\"tool_calls\"}]}\n\n"
            "data: [DONE]\n\n";
        QString d1 = p.feed(chunk1);
        QCOMPARE(d1, QStringLiteral("你好"));
        QString d2 = p.feed(chunk2);
        QCOMPARE(d2, QStringLiteral("，世界"));
        QVERIFY(p.done());
        QCOMPARE(p.finishReason(), QStringLiteral("tool_calls"));

        const QJsonObject msg = p.assistantMessage();
        QCOMPARE(msg.value("role").toString(), QStringLiteral("assistant"));
        QCOMPARE(msg.value("content").toString(), QStringLiteral("你好，世界"));
        const QJsonArray tcs = msg.value("tool_calls").toArray();
        QCOMPARE(tcs.size(), 1);
        QCOMPARE(tcs.at(0).toObject().value("id").toString(), QStringLiteral("call_1"));
        QCOMPARE(tcs.at(0).toObject().value("function").toObject().value("name").toString(),
                 QStringLiteral("shell.run"));
        QCOMPARE(tcs.at(0).toObject().value("function").toObject().value("arguments").toString(),
                 QStringLiteral("{\"command\":\"git status\"}"));
    }

    // ---- buildChatRequestBody：请求体构造 ----
    void llm_request_body()
    {
        const QJsonArray messages{QJsonObject{{"role", "system"}, {"content", "sys"}},
                                  QJsonObject{{"role", "user"}, {"content", "hi"}}};
        const QJsonArray tools{QJsonObject{
            {"type", "function"},
            {"function", QJsonObject{{"name", "t"}, {"description", "d"},
                                     {"parameters", QJsonObject{{"type", "object"}}}}}}};
        const QJsonDocument doc =
            QJsonDocument::fromJson(buildChatRequestBody("m1", messages, tools, true));
        const QJsonObject body = doc.object();
        QCOMPARE(body.value("model").toString(), QStringLiteral("m1"));
        QCOMPARE(body.value("stream").toBool(), true);
        QCOMPARE(body.value("tools").toArray().size(), 1);
        QCOMPARE(body.value("messages").toArray().size(), 2);
        // 无工具时不应携带 tools 字段
        const QJsonObject bare =
            QJsonDocument::fromJson(buildChatRequestBody("m1", messages, {}, true)).object();
        QVERIFY(!bare.contains("tools"));
    }

    // ---- PermissionManager：三模式决策 ----
    void permission_manager_decide()
    {
        PermissionManager pm(nullptr);
        const QJsonObject shellArgs{{"command", "Remove-Item foo"}};
        const QJsonObject safeShell{{"command", "git status"}};
        const QJsonObject writeArgs{{"path", "C:/x.txt"}, {"content", "hi"}};

        pm.setMode(PermissionManager::Mode::Ask);
        QCOMPARE(int(pm.decide("fs.read", {})), int(PermissionManager::Decision::Allow));
        QCOMPARE(int(pm.decide("monitor.query", {})), int(PermissionManager::Decision::Allow));
        QCOMPARE(int(pm.decide("fs.write", writeArgs)),
                 int(PermissionManager::Decision::Confirm));
        QCOMPARE(int(pm.decide("shell.run", shellArgs)),
                 int(PermissionManager::Decision::Confirm));

        // "总是允许此类"（Ask 模式下也放行该工具名）
        pm.grantAlways(QStringLiteral("shell.run"));
        QCOMPARE(int(pm.decide("shell.run", shellArgs)),
                 int(PermissionManager::Decision::Allow));
        pm.revokeAlways(QStringLiteral("shell.run"));

        // Smart：安全命令前缀放行，其余确认
        pm.setMode(PermissionManager::Mode::Smart);
        pm.setShellAllowPrefixes(QStringList{"git status", "dir"});
        QCOMPARE(int(pm.decide("shell.run", safeShell)),
                 int(PermissionManager::Decision::Allow));
        QCOMPARE(int(pm.decide("shell.run", shellArgs)),
                 int(PermissionManager::Decision::Confirm));
        QCOMPARE(int(pm.decide("fs.delete", writeArgs)),
                 int(PermissionManager::Decision::Confirm));

        // Auto：一律放行
        pm.setMode(PermissionManager::Mode::Auto);
        QCOMPARE(int(pm.decide("fs.write", writeArgs)),
                 int(PermissionManager::Decision::Allow));
    }

    // ---- ChatSession：持久化 / system 重建 / 截断 / 工具结果序列化 ----
    void chat_session_roundtrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("chat/sessions.json");

        {
            ChatSession s(path);
            s.setSystemPrompt("v1");
            s.append(QJsonObject{{"role", "user"}, {"content", "a"}});
            s.append(QJsonObject{{"role", "assistant"}, {"content", "b"}});
            s.appendToolResult("call_1", QJsonObject{{"ok", true}, {"output", "done"}});
            s.save();
            QCOMPARE(s.size(), 4);
        }
        ChatSession s2(path);
        s2.load();
        QCOMPARE(s2.size(), 4);
        QCOMPARE(s2.messages().first().toObject().value("content").toString(),
                 QStringLiteral("v1"));
        const QJsonObject toolMsg = s2.messages().last().toObject();
        QCOMPARE(toolMsg.value("role").toString(), QStringLiteral("tool"));
        QCOMPARE(toolMsg.value("tool_call_id").toString(), QStringLiteral("call_1"));
        QVERIFY(toolMsg.value("content").toString().contains(QStringLiteral("done")));

        // system 每轮重建（替换不追加）
        s2.setSystemPrompt("v2");
        QCOMPARE(s2.messages().first().toObject().value("content").toString(),
                 QStringLiteral("v2"));
        QCOMPARE(s2.size(), 4);

        // 历史截断：system 之外保留最近 40 条
        for (int i = 0; i < 60; ++i)
            s2.append(QJsonObject{{"role", "user"}, {"content", QString::number(i)}});
        QCOMPARE(s2.size(), 41); // 1 system + 40
        QCOMPARE(s2.messages().at(1).toObject().value("content").toString(),
                 QStringLiteral("20")); // 最早 20 条被裁

        // 清空后 save/load 往返
        s2.clear();
        s2.save();
        ChatSession s3(path);
        s3.load();
        QCOMPARE(s3.size(), 0);
    }

    // ---- ToolRegistry：注册 / schema / 异步执行 / 未知工具 ----
    void tool_registry_invoke()
    {
        ToolRegistry r;
        ToolDef d;
        d.name = QStringLiteral("echo");
        d.readOnly = true;
        d.description = QStringLiteral("echo args");
        d.parameters = QJsonObject{{"type", QStringLiteral("object")}};
        d.fn = [](const QJsonObject &args, const std::function<void(QJsonObject)> &done) {
            done(QJsonObject{{"ok", true}, {"echoed", args.value("x").toString()}});
        };
        r.registerTool(d);

        QVERIFY(r.has("echo"));
        QVERIFY(r.isReadOnly("echo"));
        QCOMPARE(r.toolSchemas().size(), 1);

        QJsonObject got;
        QMetaObject::invokeMethod(&r, [&] {
            r.invoke("echo", QJsonObject{{"x", "hello"}},
                     [&got](const QJsonObject &res) { got = res; });
        }, Qt::DirectConnection);
        QCOMPARE(got.value("echoed").toString(), QStringLiteral("hello"));

        // 未知工具：立即回调错误结果
        QJsonObject miss;
        r.invoke("nope", {}, [&miss](const QJsonObject &res) { miss = res; });
        QVERIFY(!miss.value("ok").toBool(true));
    }

    // ---- ToolRegistry：API 名转写往返（DeepSeek 只允许 [a-zA-Z0-9_-]） ----
    void tool_registry_api_name()
    {
        ToolRegistry r;
        ToolDef d;
        d.name = QStringLiteral("fs.read");
        d.description = QStringLiteral("read file");
        d.parameters = QJsonObject{{"type", QStringLiteral("object")}};
        d.fn = [](const QJsonObject &, const std::function<void(QJsonObject)> &done) {
            done(QJsonObject{{"ok", true}});
        };
        r.registerTool(d);
        ToolDef d2;
        d2.name = QStringLiteral("mcp__srv__search"); // MCP 名本身合法，应原样保留
        d2.description = QStringLiteral("mcp tool");
        d2.parameters = QJsonObject{{"type", QStringLiteral("object")}};
        d2.fn = [](const QJsonObject &, const std::function<void(QJsonObject)> &done) {
            done(QJsonObject{{"ok", true}});
        };
        r.registerTool(d2);

        // schema 输出用转写名：fs.read → fs_read；MCP 名不变
        const QJsonArray schemas = r.toolSchemasForApi();
        QCOMPARE(schemas.size(), 2);
        bool sawUnderscore = false, sawMcp = false;
        for (const QJsonValue &v : schemas) {
            const QString n = v.toObject().value("function").toObject().value("name").toString();
            if (n == QStringLiteral("fs_read"))
                sawUnderscore = true;
            if (n == QStringLiteral("mcp__srv__search"))
                sawMcp = true;
            QVERIFY(!n.contains(QLatin1Char('.'))); // 全部满足 DeepSeek 模式
        }
        QVERIFY(sawUnderscore && sawMcp);

        // 回包还原：fs_read → fs.read；原样名不受影响
        QCOMPARE(r.fromApiName(QStringLiteral("fs_read")), QStringLiteral("fs.read"));
        QCOMPARE(r.fromApiName(QStringLiteral("mcp__srv__search")),
                 QStringLiteral("mcp__srv__search"));
        QCOMPARE(r.fromApiName(QStringLiteral("unknown_x")), QStringLiteral("unknown_x"));
    }

    // ---- DailyAggregator：JSONL → LLM 摘要（线程池读取 → 主线程回调） ----
    void daily_aggregator_digest()
    {
        QTemporaryDir base;
        QVERIFY(base.isValid());
        const QString dayDir = base.path() + "/2026-11-08";
        QVERIFY(QDir().mkpath(dayDir));

        QFile tl(dayDir + "/timeline.jsonl");
        QVERIFY(tl.open(QIODevice::WriteOnly | QIODevice::Text));
        // 60s 切片的两段（应合并为一行）+ 一段别的窗口
        tl.write("{\"start\":1731000000000,\"end\":1731000060000,\"app\":\"code.exe\","
                 "\"title\":\"main.cpp\",\"activeSec\":60,\"keystrokes\":80,\"clicks\":2}\n");
        tl.write("{\"start\":1731000120000,\"end\":1731000180000,\"app\":\"code.exe\","
                 "\"title\":\"main.cpp\",\"activeSec\":60,\"keystrokes\":50,\"clicks\":1}\n");
        tl.write("{\"start\":1731000600000,\"end\":1731000900000,\"app\":\"chrome.exe\","
                 "\"title\":\"文档\",\"activeSec\":300,\"keystrokes\":0,\"clicks\":12}\n");
        tl.close();

        QFile keys(dayDir + "/keys.jsonl");
        QVERIFY(keys.open(QIODevice::WriteOnly | QIODevice::Text));
        keys.write("{\"ts\":1731000030000,\"app\":\"code.exe\",\"title\":\"main.cpp\","
                   "\"text\":\"fix hook timeout bug\"}\n");
        keys.close();

        QString digest;
        bool ok = false;
        DailyAggregator::buildDigestAsync(dayDir, 12000, this,
                                          [&](bool o, QString d) { ok = o; digest = d; });
        QTRY_VERIFY(ok); // 线程池回调经主线程队列交付
        QVERIFY(digest.contains(QStringLiteral("code.exe — main.cpp")));
        QVERIFY(digest.contains(QStringLiteral("键入")));
        QVERIFY(digest.contains(QStringLiteral("fix hook timeout bug")));
        // 两段切片合并后 keystrokes 相加（80+50）
        QVERIFY(digest.contains(QStringLiteral("键入130")));
        QVERIFY(digest.contains(QStringLiteral("chrome.exe")));
    }

    // ---- Scheduler：每日任务当天只跑一次 ----
    void scheduler_daily_once()
    {
        Scheduler s;
        int runs = 0;
        s.setDailyTask(QStringLiteral("t"), QTime(0, 0), [&runs] { ++runs; });
        s.start();
        // 手动驱动心跳（QTimer 周期 60s，测试不真等）：00:00 必然已过 → 触发
        s.tick();
        QCOMPARE(runs, 1);
        s.tick(); // 同一天内不重复触发
        QCOMPARE(runs, 1);
    }

    // ---- MemoryStore：长期记忆（index 引用 + 正文按需） ----
    void memory_store_roundtrip()
    {
        QTemporaryDir base;
        QVERIFY(base.isValid());
        QString id1, id2;
        {
            MemoryStore mem(base.path());
            id1 = mem.addEntry(QStringLiteral("fact"), {QStringLiteral("偏好"), QStringLiteral("编辑器")},
                               QStringLiteral("用户喜欢 VSCode 深色主题"));
            id2 = mem.addEntry(QStringLiteral("ref"), {QStringLiteral("日志")},
                               QStringLiteral("每日记录见 goody/daily"));
            QVERIFY(!id1.isEmpty() && !id2.isEmpty());
            QVERIFY(id1 != id2);
            QVERIFY(mem.indexSummary().contains(QStringLiteral("VSCode")));
        }
        // 重新装载：index 引用还在，正文按需可读
        MemoryStore mem(base.path());
        mem.load();
        QCOMPARE(mem.entries().size(), 2);
        QVERIFY(mem.entryContent(id1).contains(QStringLiteral("深色主题")));
        QVERIFY(mem.entryContent(id1).startsWith(QStringLiteral("用户喜欢"))); // frontmatter 已剥

        // 标签命中 + touch 统计
        const QList<MemoryStore::SearchHit> hits = mem.search(QStringLiteral("偏好"));
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first().meta.id, id1);
        QVERIFY(!hits.first().excerpt.isEmpty());

        // 删除
        QVERIFY(mem.removeEntry(id2));
        QCOMPARE(mem.entries().size(), 1);
        QVERIFY(mem.entryContent(id2).isEmpty());

        // id 非法字符防路径穿越
        const QString evil = mem.entryContent(QStringLiteral("../../etc/passwd"));
        QVERIFY(evil.isEmpty());
    }

    // ---- WorkingMemory：缓存命中 / 归档释放 ----
    void working_memory_cache_hit_and_archive()
    {
        QTemporaryDir base;
        QVERIFY(base.isValid());

        WorkingMemory work(base.path());
        // 新建
        const QJsonObject t1 = work.startTask(QStringLiteral("收纳盒整理"),
                                              QStringLiteral("把桌面文件分类"));
        QCOMPARE(t1.value("continued").toBool(), false);
        QVERIFY(!t1.value("id").toString().isEmpty());
        // 缓存命中：相近标题续用同一任务
        const QJsonObject t2 = work.startTask(QStringLiteral("继续收纳盒整理"),
                                              QStringLiteral("换目标"));
        QCOMPARE(t2.value("continued").toBool(), true);
        QCOMPARE(t2.value("id").toString(), t1.value("id").toString());
        work.updateTask(t1.value("id").toString(), QStringLiteral("已建 3 个盒子"));
        QCOMPARE(work.openCount(), 1);
        QVERIFY(work.promptBlock().contains(QStringLiteral("收纳盒整理")));

        // 重新装载（模拟重启）：未结任务仍在
        WorkingMemory work2(base.path());
        QCOMPARE(work2.openCount(), 1);
        QVERIFY(work2.listTasks().value("tasks").toArray().size() == 1);

        // 完成 → 归档 + 工作集释放
        const QJsonObject done = work2.completeTask(t1.value("id").toString(),
                                                    QStringLiteral("整理完成，共 5 盒"));
        QCOMPARE(done.value("ok").toBool(), true);
        QVERIFY(QFileInfo::exists(done.value("archived").toString()));
        QCOMPARE(work2.openCount(), 0);
        QFile archived(done.value("archived").toString());
        QVERIFY(archived.open(QIODevice::ReadOnly));
        const QString md = QString::fromUtf8(archived.readAll());
        archived.close();
        QVERIFY(md.contains(QStringLiteral("整理完成")));
        QVERIFY(md.contains(QStringLiteral("已建 3 个盒子"))); // 过程笔记入档
    }

    // ---- McpStdioClient::loadConfig：配置解析与容错 ----
    void mcp_config_load()
    {
        QTemporaryDir base;
        QVERIFY(base.isValid());
        // 缺文件 → 空表（MCP 可选能力）
        QVERIFY(McpStdioClient::loadConfig(base.path()).isEmpty());

        const QString cfg = base.filePath("mcp.json");
        QFile f(cfg);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"servers\":["
                "{\"name\":\"fs\",\"command\":\"npx\",\"args\":[\"-y\",\"@modelcontextprotocol/server-filesystem\",\"D:/\"]},"
                "{\"name\":\"坏的\",\"command\":\"\"},"
                "{\"name\":\"py\",\"command\":\"python\",\"args\":[\"-m\",\"mcp_server\"]}]}");
        f.close();
        const QList<McpStdioClient::ServerConfig> servers = McpStdioClient::loadConfig(base.path());
        QCOMPARE(servers.size(), 2); // command 为空的条目被跳过
        QCOMPARE(servers.first().name, QStringLiteral("fs"));
        QCOMPARE(servers.first().args.size(), 3);
        QCOMPARE(servers.last().command, QStringLiteral("python"));
    }
};

QTEST_MAIN(DomainTests)
#include "domain_tests.moc"
