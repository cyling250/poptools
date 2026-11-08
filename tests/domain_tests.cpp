#pragma once

#include <QtTest/QtTest>

#include <memory>
#include <vector>

#include "app/AppServices.h"
#include "app/FeatureRegistry.h"
#include "core/annotation/AnnotationCanvas.h"
#include "core/annotation/AnnotationItem.h"
#include "core/calendar/CalendarStore.h"
#include "core/calendar/LunarCalendar.h"
#include "core/capture/QrScanner.h"
#include "core/hotkey/HotKeyNames.h"
#include "core/settings/Settings.h"
#include "core/translation/Languages.h"
#include "core/translation/Translator.h"
#include "ui/ThemeColors.h"

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

    // ---- 桌面日历：CalendarStore 增删改/排序/分类 JSON 往返 ----
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
        const QString todo1 = store.addTodo(day, workId, QStringLiteral("写周报"), true);
        const QString todo2 = store.addTodo(day, workId, QStringLiteral("买咖啡"), false);
        const QString sched1 = store.addSchedule(day, QString(), QStringLiteral("开题会"), false);
        QVERIFY(!todo1.isEmpty() && !todo2.isEmpty() && !sched1.isEmpty());

        QCOMPARE(store.itemsForDate(day, true).size(), 2);
        QCOMPARE(store.itemsForDate(day, false).size(), 1);

        // 完成切换
        QVERIFY(store.setDone(todo1, true));
        QVERIFY(!store.setDone(todo1, true)); // 幂等
        QCOMPARE(store.itemsForDate(day, true).at(0).done, true);

        // 上移/下移：moveItem(-1) 把 todo2 提到 todo1 前
        QVERIFY(store.moveItem(todo2, -1));
        QCOMPARE(store.itemsForDate(day, true).at(0).id, todo2);
        QVERIFY(!store.moveItem(todo2, -1)); // 已在顶
        QVERIFY(store.moveItem(todo2, 1));
        QCOMPARE(store.itemsForDate(day, true).at(0).id, todo1);

        // 安排到其他日期
        QVERIFY(store.moveItemToDate(todo2, day.addDays(1)));
        QCOMPARE(store.itemsForDate(day, true).size(), 1);
        QCOMPARE(store.itemsForDate(day.addDays(1), true).size(), 1);

        // 拖拽重排回写（先补第三项，取到 2 项再做重排）
        store.addTodo(day, workId, QStringLiteral("第三项"), false);
        const QList<CalendarItem> todos = store.itemsForDate(day, true);
        QVERIFY(todos.size() == 2);
        QStringList newOrder{ todos.at(1).id, todos.at(0).id };
        store.reorderItems(day, true, newOrder);
        QCOMPARE(store.itemsForDate(day, true).at(0).id, newOrder.at(0));
        QCOMPARE(store.itemsForDate(day, true).at(1).id, newOrder.at(1));

        // 分类增改删（删除后条目改挂默认分类）
        const QString tempCat = store.addCategory(QStringLiteral("临时"), QColor("#123456"));
        QVERIFY(!tempCat.isEmpty());
        QVERIFY(store.updateCategory(tempCat, QStringLiteral("临时改"), QColor("#654321")));
        store.addSchedule(day, tempCat, QStringLiteral("带临时分类"), false);
        QVERIFY(store.removeCategory(tempCat));
        QVERIFY(!store.removeCategory(tempCat)); // 再删无效
        QVERIFY(!store.removeCategory(CalendarStore::defaultCategoryId())); // 默认分类不可删
        for (const CalendarItem &it : store.itemsForDate(day, false))
            QVERIFY(it.categoryId != tempCat);

        // JSON 往返：重新加载同一路径，条目与属性一致
        const QList<CalendarItem> before = store.items();
        const QList<CalendarCategory> catsBefore = store.categories();
        CalendarStore reloaded(path);
        reloaded.load();
        QCOMPARE(reloaded.items().size(), before.size());
        QCOMPARE(reloaded.categories().size(), catsBefore.size());
        QCOMPARE(reloaded.itemsForDate(day, true).size(), 2);
        QCOMPARE(reloaded.itemsForDate(day.addDays(1), true).size(), 1);
        // 待办完成态与重要标记均持久化
        for (const CalendarItem &it : reloaded.itemsForDate(day, true)) {
            if (it.id == todo1)
                QVERIFY(it.done);
            if (it.id == todos.at(0).id)
                QCOMPARE(it.important, true);
        }
    }
};

QTEST_MAIN(DomainTests)
#include "domain_tests.moc"
