#pragma once

#ifdef _WIN32 // Q_OS_WIN 要等 Qt 头才定义，此处用编译器内置宏
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> // OwnWriteGuard 用例读 GetClipboardSequenceNumber（先于 Qt）
#endif

#include <QtTest/QtTest>

#include <memory>
#include <vector>
#include "core/annotation/AnnotationCanvas.h"
#include "core/calendar/CalendarLayout.h"
#include "core/box/BoxLayout.h"
#include "core/box/BoxCatalog.h"
#include "mcpserver/McpTools.h"
#include "core/calendar/CalendarSettings.h"
#include "core/calendar/CalendarTaskStore.h"
#include "core/calendar/ChineseCalendar.h"
#include "core/annotation/AnnotationItem.h"
#include "core/annotation/AnnotationPainter.h"
#include "core/capture/QrScanner.h"
#include "core/hotkey/HotKeyNames.h"
#include "core/monitor/ActivityStore.h"
#include "core/monitor/ClipboardCodec.h"
#include "core/monitor/ImeCommitDetector.h"
#include "core/monitor/InputActivityMonitor.h"
#include "core/monitor/OwnWriteGuard.h"
#include "core/monitor/SnapshotOcrPipeline.h"
#include "core/monitor/TextMirrorMonitor.h"
#include "core/monitor/UiaTextReader.h"
#include "core/ocr/RapidOcrService.h"
#include "core/settings/Settings.h"
#include "core/translation/Languages.h"
#include "core/translation/Translator.h"
#include "ui/ThemeColors.h"
#include "ui/theming/FluentTheme.h"
#include "ui/goody/GoodyBallLogic.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
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

    void canvas_undo_stack_capped()
    {
        // 撤销栈上限 20 步 FIFO 淘汰（R3 #6）：长会话全量快照不再无界增长
        AnnotationCanvas c;
        AnnotationItem it{};
        it.rect = QRectF(0, 0, 10, 10);
        for (int i = 0; i < 25; ++i)
            c.addItem(it); // 每次入撤销栈

        for (int i = 0; i < 20; ++i) {
            QVERIFY(c.canUndo());
            c.undo();
        }
        QVERIFY(!c.canUndo()); // 最早的 5 个快照已被淘汰（空态不再可回退）
        QCOMPARE(c.items.size(), 5);

        // 重做链完整：上限内淘汰的只是撤销历史，重做仍能回到最新
        for (int i = 0; i < 20; ++i) {
            QVERIFY(c.canRedo());
            c.redo();
        }
        QVERIFY(!c.canRedo());
        QCOMPARE(c.items.size(), 25);
    }

    // ---- AnnotationPainter：马赛克缩放预览帧跳过重采样（R3 #24） ----
    void mosaic_scaled_preview_skips_resample()
    {
        AnnotationItem it{};
        it.type = AnnotationType::Mosaic;
        it.rect = QRectF(0, 0, 100, 100);
        it.mosaicImage = QImage(200, 200, QImage::Format_ARGB32);
        it.mosaicImage.fill(Qt::red);

        // 预览帧（拖拽中间帧）：mosaicImage 保持起始图原样（COW 零拷贝，
        // 绘制端按目标矩形拉伸），不做整幅 SmoothTransformation
        const AnnotationItem preview =
            AnnotationPainter::scaledItem(it, QRectF(0, 0, 50, 50), false, false, true);
        QCOMPARE(preview.mosaicImage.size(), QSize(200, 200));

        // 定稿帧（松手）：随框等比缩放到新边界
        const AnnotationItem finalItem =
            AnnotationPainter::scaledItem(it, QRectF(0, 0, 50, 50), false, false, false);
        QCOMPARE(finalItem.mosaicImage.size(), QSize(100, 100));
    }

    // ---- AnnotationPainter：富文本项可重复绘制（布局缓存路径，R3 #25） ----
    void rich_text_draw_repeated_no_crash()
    {
        AnnotationItem it{};
        it.type = AnnotationType::Text;
        it.html = QStringLiteral("<span style=\"font-size:20px\">Hello <b>World</b></span>");
        it.font = QFont(QStringLiteral("Arial"), 12);
        it.color = Qt::black;
        it.rect = QRectF(0, 0, 200, 40);
        QImage img(200, 40, QImage::Format_ARGB32);
        img.fill(Qt::white);
        QPainter p(&img);
        AnnotationPainter::drawOne(p, it); // 首次：建文档入缓存
        AnnotationPainter::drawOne(p, it); // 再次：命中缓存，输出一致
        p.end();
    }

    // ---- RapidOcrService：OCR 行序重排（半行高容差同行判定，R3 #8） ----
    void ocr_reading_order_half_row_tolerance()
    {
        // 同一文本行的 y 微差（≤半行高）不拆行：行内按 left 排
        QVector<OcrBox> boxes;
        OcrBox a; a.rect = QRect(200, 100, 50, 20); a.text = QStringLiteral("B");
        OcrBox b; b.rect = QRect(10, 103, 40, 20); b.text = QStringLiteral("A"); // 与 a 同行（top 微差 3px）
        OcrBox c; c.rect = QRect(0, 140, 30, 20); c.text = QStringLiteral("C");  // 下一行
        boxes << a << b << c;
        sortOcrBoxesReadingOrder(boxes);
        QCOMPARE(boxes[0].text, QStringLiteral("A"));
        QCOMPARE(boxes[1].text, QStringLiteral("B"));
        QCOMPARE(boxes[2].text, QStringLiteral("C"));

        // 超出半行高容差（|Δtop| > 行高中点）：按上下行排序
        QVector<OcrBox> twoLines;
        OcrBox low; low.rect = QRect(0, 100 + 20, 30, 20); low.text = QStringLiteral("low");
        OcrBox high; high.rect = QRect(0, 100, 30, 20); high.text = QStringLiteral("high");
        twoLines << low << high;
        sortOcrBoxesReadingOrder(twoLines);
        QCOMPARE(twoLines[0].text, QStringLiteral("high"));
        QCOMPARE(twoLines[1].text, QStringLiteral("low"));
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

    // ---- FluentTokens（2026-11-19 Fluent 改版：token 表完整性 + 明暗两套
    // 关键值；原 ThemeColors"无灰字"约定已被 Fluent 文本四档取代） ----
    void theme_colors_light_and_dark()
    {
        const FluentTokens light = fluentTheme(false);
        const FluentTokens dark = fluentTheme(true);
        QVERIFY(!light.dark);
        QVERIFY(dark.dark);
        // 基底：浅色 #F3F3F3 / 深色 #202020（Win11 SolidBackgroundFillColorBase）
        QCOMPARE(QByteArray(light.bgBase), QByteArray("#F3F3F3"));
        QCOMPARE(QByteArray(dark.bgBase), QByteArray("#202020"));
        // 卡面：浅色纯白 / 深色 #2A2A2A（Win11 CardBackgroundFillColorDefault）
        QCOMPARE(QByteArray(light.bgCard), QByteArray("#FFFFFF"));
        QCOMPARE(QByteArray(dark.bgCard), QByteArray("#2A2A2A"));
        // 文本四档齐全（主/次/三级/禁用），主文本浅深反转
        QVERIFY(light.textPrimary && light.textSecondary
                && light.textTertiary && light.textDisabled);
        QVERIFY(dark.textPrimary && dark.textSecondary
                && dark.textTertiary && dark.textDisabled);
        QCOMPARE(QByteArray(dark.textPrimary), QByteArray("#FFFFFF"));
        QVERIFY(QByteArray(light.textPrimary) != QByteArray(dark.textPrimary));
        // 品牌主色保留：浅色 #2B6FF2；深色套预提亮为 #5B93FF；accent 实底文字白
        QCOMPARE(QByteArray(light.accent), QByteArray("#2B6FF2"));
        QCOMPARE(QByteArray(dark.accent), QByteArray("#5B93FF"));
        QCOMPARE(QByteArray(light.onAccent), QByteArray("#FFFFFF"));
        // 低透明选区底（#AARRGGBB）与描边/分隔线齐全
        QVERIFY(QByteArray(light.accentSoft).startsWith("#"));
        QVERIFY(light.cardStroke && *light.cardStroke);
        QVERIFY(dark.cardStroke && *dark.cardStroke);
        QVERIFY(light.divider && *light.divider);
        QVERIFY(dark.divider && *dark.divider);
        // 控件四态齐全（底/描边/悬停/按下）
        QVERIFY(light.ctrlBg && light.ctrlBorder && light.ctrlHover && light.ctrlPressed);
        QVERIFY(dark.ctrlBg && dark.ctrlBorder && dark.ctrlHover && dark.ctrlPressed);
        // 错误/成功语义 + 滚动条两档齐全
        QVERIFY(light.errText && light.errBg && light.successText);
        QVERIFY(dark.errText && dark.errBg && dark.successText);
        QVERIFY(light.scrollThumb && light.scrollThumbHover);
        QVERIFY(dark.scrollThumb && dark.scrollThumbHover);
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

    void settings_hotkey_modifiers_validated()
    {
        // 修饰键合法性校验（R3 #13，热键只能手改 INI，无 UI 校验）：
        // 只认 MOD_ALT|MOD_CONTROL|MOD_SHIFT|MOD_WIN 的非零组合，
        // 0（无修饰键全局吞键）与含未知位回退默认
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Settings s(dir.filePath("test_mod.ini"));

        s.setSelectionTranslateHotKey(0x58, 0x0007);
        QCOMPARE(s.selectionTranslateHotKeyModifiers(), quint32(0x0007));
        s.setSelectionTranslateHotKey(0x58, 0);
        QCOMPARE(s.selectionTranslateHotKeyModifiers(), quint32(0x0007));
        s.setSelectionTranslateHotKey(0x58, 0x10 /* 未知位 */);
        QCOMPARE(s.selectionTranslateHotKeyModifiers(), quint32(0x0007));

        s.setAssistantHotKey(0x47, 0x0003);
        QCOMPARE(s.assistantHotKeyModifiers(), quint32(0x0003));
        s.setAssistantHotKey(0x47, 0x8 /* MOD_WIN 单独使用合法 */);
        QCOMPARE(s.assistantHotKeyModifiers(), quint32(0x8));
        s.setAssistantHotKey(0x47, 0x4000 /* MOD_NOREPEAT 等未知位 */);
        QCOMPARE(s.assistantHotKeyModifiers(), quint32(0x0003));
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
            s.setMonitorIdleThresholdSec(5);  // 低于下限 → 钳到 10
            s.setMonitorRetentionDays(99999); // 超上限 → 钳到 3650
            s.setMonitorOcrSnapshot(true);
        }
        Settings s(ini);
        QVERIFY(s.monitorEnabled());
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

    // ============================================================
    // Round1 审查修复回归（P0-1/P0-2/P0-3/P1-1/P1-2/P1-6/P1-9）
    // ============================================================

    // ---- ActivityStore 回归：任意流全量落盘（audit 曾因白名单从未落盘）
    //      + stop() 余量落盘（守卫顺序曾丢最后一批缓冲） ----
    void activity_store_audit_stream_and_stop_flush()
    {
        QTemporaryDir base;
        QVERIFY(base.isValid());
        const QString dayDir =
            base.path() + QLatin1Char('/') + QDate::currentDate().toString("yyyy-MM-dd");

        // audit 流（非 monitor 三流的任意流名）必须照常落盘
        {
            ActivityStore store(base.path());
            store.start();
            QJsonObject audit;
            audit.insert("tool", QStringLiteral("shell.run"));
            audit.insert("decision", QStringLiteral("confirm"));
            store.append(QStringLiteral("audit"), audit);
            store.flushBlocking();
            QFile auditFile(dayDir + QStringLiteral("/audit.jsonl"));
            QVERIFY(auditFile.open(QIODevice::ReadOnly));
            const QJsonObject line =
                QJsonDocument::fromJson(auditFile.readAll().split('\n').first()).object();
            QCOMPARE(line.value("tool").toString(), QStringLiteral("shell.run"));
        }

        // 不显式 flush 直接 stop：缓冲余量由 stop() 内部先 flush 再停线程
        ActivityStore store2(base.path());
        store2.start();
        QJsonObject ev;
        ev.insert("ts", 1);
        store2.append(QStringLiteral("keys"), ev);
        store2.append(QStringLiteral("audit"), ev);
        QCOMPARE(store2.bufferedLines(), 2);
        store2.stop();
        QFile keys(dayDir + QStringLiteral("/keys.jsonl"));
        QVERIFY(keys.open(QIODevice::ReadOnly));
        QVERIFY(keys.readAll().size() > 2); // 余量已写（此前 stop 会整批丢弃）
        keys.close();
        QFile auditFile2(dayDir + QStringLiteral("/audit.jsonl"));
        QVERIFY(auditFile2.open(QIODevice::ReadOnly));
        QVERIFY(auditFile2.readAll().contains(QByteArray("\"ts\":1")));
    }

    // ---- Settings：悬浮球键默认值/往返 + 手改 ini 未引号列表容错（T-050） ----
    void settings_ball_keys_and_unquoted_tolerance()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath("settings_ball.ini");
        {
            Settings s(ini);
            QVERIFY(s.assistantBallEnabled()); // 默认开
            QVERIFY(s.assistantBallPos().isEmpty()); // 默认无记忆位
            s.setAssistantBallEnabled(false);
            s.setAssistantBallPos(goodyball::ballPosToString(QPoint(123, -456)));
        }
        {
            Settings s(ini);
            QVERIFY(!s.assistantBallEnabled());
            QCOMPARE(goodyball::ballPosFromString(s.assistantBallPos()),
                     QPoint(123, -456));
        }
        // 手改 ini 未加引号："ballPos=123,-456"（无引号）会被 IniFormat 按
        // 逗号裂成 QStringList，直接 toString() 得空串（实测踩坑 P-0xx）——
        // 读取端须把双元素列表合回 "x,y" 字符串
        {
            QFile f(ini);
            QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
            QByteArray content = f.readAll();
            f.close();
            content.replace("ballPos=\"123,-456\"", "ballPos=123,-456");
            QVERIFY(content.contains("ballPos=123,-456")); // 替换确实命中引号形态
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
            f.write(content);
            f.close();
        }
        Settings s(ini);
        QCOMPARE(goodyball::ballPosFromString(s.assistantBallPos()), QPoint(123, -456));
        // 非双元素的手改坏列表 → 仍回落空串（不崩不误用）
        {
            QFile f(ini);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
            f.write("[assistant]\nballPos=1,2,3\n");
            f.close();
        }
        Settings s3(ini);
        QVERIFY(s3.assistantBallPos().isEmpty());
    }

    // ---- GoodyBallLogic：点击/拖动判别状态机（T-050，注入位移） ----
    void goody_ball_gesture_click_vs_drag()
    {
        using goodyball::Gesture;
        using goodyball::GestureClassifier;

        // 按下未动 → 单击
        GestureClassifier g;
        g.beginPress(QPoint(100, 100));
        QVERIFY(!g.dragging());
        QCOMPARE(g.endRelease(), Gesture::Click);

        // 未按松开 / 重复松开 → None（无按下语义）
        QCOMPARE(g.endRelease(), Gesture::None);
        g.addMove(QPoint(200, 200)); // 空闲态移动安全无操作
        QCOMPARE(g.endRelease(), Gesture::None);

        // 微动（≤6px 曼哈顿距离）仍判单击
        g.beginPress(QPoint(0, 0));
        g.addMove(QPoint(2, 3)); // 5 ≤ 6
        g.addMove(QPoint(1, 2)); // 相对按下点 3 ≤ 6
        QVERIFY(!g.dragging());
        QCOMPARE(g.endRelease(), Gesture::Click);

        // 阈值边界：恰好 6px 单击，7px 拖动
        g.beginPress(QPoint(0, 0));
        g.addMove(QPoint(6, 0));
        QCOMPARE(g.endRelease(), Gesture::Click);
        g.beginPress(QPoint(0, 0));
        g.addMove(QPoint(7, 0));
        QVERIFY(g.dragging());
        QCOMPARE(g.endRelease(), Gesture::Drag);

        // 拖动跟手：totalDelta 随注入移动推进
        g.beginPress(QPoint(0, 0));
        g.addMove(QPoint(4, 4)); // 8 > 6：闩锁
        g.addMove(QPoint(20, 15));
        QCOMPARE(g.totalDelta(), QPoint(20, 15));
        QCOMPARE(g.endRelease(), Gesture::Drag);

        // 闩锁语义：拖动后回落到按下点附近仍判拖动（不因终点≈起点改判单击）
        g.beginPress(QPoint(50, 50));
        g.addMove(QPoint(60, 50));
        QVERIFY(g.dragging());
        g.addMove(QPoint(50, 50));
        QVERIFY(g.dragging());
        QCOMPARE(g.endRelease(), Gesture::Drag);
    }

    // ---- GoodyBallLogic：位置钳制（注入屏幕矩形，T-050） ----
    void goody_ball_pos_clamp()
    {
        const QSize sz(52, 52);
        const QList<QRect> screens{QRect(0, 0, 1920, 1040), QRect(1920, 0, 1920, 1080)};

        // 屏内原样保留
        QCOMPARE(goodyball::clampBallPos(QPoint(100, 100), sz, screens), QPoint(100, 100));
        // 第二屏归属（球中心在 1920..3840）且未越界 → 原样
        QCOMPARE(goodyball::clampBallPos(QPoint(2000, 500), sz, screens), QPoint(2000, 500));
        // 第二屏右越界 → 钳回第二屏右缘内（不跳回第一屏）
        QCOMPARE(goodyball::clampBallPos(QPoint(3800, 500), sz, screens),
                 QPoint(1920 + 1920 - 52, 500));
        // 下越界 → 钳回第一屏底缘内
        QCOMPARE(goodyball::clampBallPos(QPoint(100, 2000), sz, screens),
                 QPoint(100, 1040 - 52));
        // 上/左负越界 → 钳回 ≥ 屏左上
        QCOMPARE(goodyball::clampBallPos(QPoint(-30, -8), sz, screens), QPoint(0, 0));
        // 完全离屏 → 最近屏钳制
        QCOMPARE(goodyball::clampBallPos(QPoint(-500, -500), sz, screens), QPoint(0, 0));
        // 负坐标屏（显示器在主屏左侧）同样成立
        const QList<QRect> leftScreens{QRect(-1920, 0, 1920, 1040)};
        QCOMPARE(goodyball::clampBallPos(QPoint(-1900, 100), sz, leftScreens),
                 QPoint(-1900, 100));
        // 屏表为空（异常环境）原样返回
        QCOMPARE(goodyball::clampBallPos(QPoint(9, 9), sz, {}), QPoint(9, 9));

        // 默认位：可用区右上角内缩 12px，整球落在可用区内（不遮任务栏）
        const QRect avail(0, 0, 1920, 1040);
        const QPoint def = goodyball::defaultBallPos(avail, sz);
        QCOMPARE(def, QPoint(1920 - 52 - 12, 12));
        QVERIFY(avail.contains(QRect(def, sz)));
    }

    // ---- GoodyBallLogic：assistant/ballPos "x,y" 字符串往返（T-050） ----
    void goody_ball_pos_string_roundtrip()
    {
        bool ok = false;
        // 正/负坐标往返无损
        const QPoint p1(123, -456);
        QCOMPARE(goodyball::ballPosFromString(goodyball::ballPosToString(p1), &ok), p1);
        QVERIFY(ok);
        // 周边空格容忍（手改 ini 常见）
        const QPoint p2 = goodyball::ballPosFromString(QStringLiteral(" 10 , 20 "), &ok);
        QVERIFY(ok);
        QCOMPARE(p2, QPoint(10, 20));
        // 坏值：空串/缺字段/非数字/多余字段 → isNull + ok=false（调用方回落默认位）
        ok = true;
        QVERIFY(goodyball::ballPosFromString(QString(), &ok).isNull());
        QVERIFY(!ok);
        QVERIFY(goodyball::ballPosFromString(QStringLiteral("12"), &ok).isNull());
        QVERIFY(!ok);
        QVERIFY(goodyball::ballPosFromString(QStringLiteral("a,b"), &ok).isNull());
        QVERIFY(!ok);
        QVERIFY(goodyball::ballPosFromString(QStringLiteral("1,2,3"), &ok).isNull());
        QVERIFY(!ok);
    }

    // ---- SnapshotOcrPipeline::consumeBaselineAdded：基线消费
    //      （尾部 300 截断 / 无新增也推进 / 超 32 窗整体清空） ----
    void ocr_consume_baseline_added_states()
    {
        // 首拍全量新增超 300 字符 → 只留尾部 300（最新输入）
        QHash<quintptr, QString> baseline;
        const QString full = QString(400, QLatin1Char('a'));
        const QString added = SnapshotOcrPipeline::consumeBaselineAdded(baseline, 1, full);
        QCOMPARE(added.size(), 300);
        QCOMPARE(added, QString(300, QLatin1Char('a')));

        // 基线已推进为全文：下次真增量正确返回
        QCOMPARE(SnapshotOcrPipeline::consumeBaselineAdded(baseline, 1, full + QStringLiteral("b")),
                 QStringLiteral("b"));

        // 无新增时基线仍推进（不是"无新增就不记"），下次真增量才对
        //（新增文本在助手里已 trim：" world" 交由上层，此处回 "world"）
        QHash<quintptr, QString> b2;
        QCOMPARE(SnapshotOcrPipeline::consumeBaselineAdded(b2, 7, QStringLiteral("hello")),
                 QStringLiteral("hello"));
        QVERIFY(SnapshotOcrPipeline::consumeBaselineAdded(b2, 7, QStringLiteral("hello"))
                    .isEmpty());
        QCOMPARE(SnapshotOcrPipeline::consumeBaselineAdded(b2, 7, QStringLiteral("hello world")),
                 QStringLiteral("world"));

        // 缓存超 32 窗整体清空：下一拍等价首拍（全量返回），旧窗基线一并失效；
        // 当前窗基线存活——T-062 修复(B-1)：原实现"先插后清"把刚插入的当前窗
        // 基线一并清掉，下一次同窗快照会把已记内容重当增量重复落盘
        QHash<quintptr, QString> b3;
        for (quintptr h = 1; h <= 32; ++h)
            SnapshotOcrPipeline::consumeBaselineAdded(b3, h, QStringLiteral("w%1").arg(h));
        QCOMPARE(b3.size(), 32);
        QCOMPARE(SnapshotOcrPipeline::consumeBaselineAdded(b3, 33, QStringLiteral("new")),
                 QStringLiteral("new"));
        QCOMPARE(b3.size(), 1); // 整体清空（防膨胀）后仅当前窗存活，不是逐窗淘汰
        QVERIFY(b3.contains(quintptr(33))); // 修复点：当前窗基线不被连根清掉
        QCOMPARE(SnapshotOcrPipeline::consumeBaselineAdded(b3, 1, QStringLiteral("w1-late")),
                 QStringLiteral("w1-late")); // 清空后旧窗再拍 = 首拍全量
    }

    // ---- ActivityStore：当日 meta 保留 / 无 meta 首建 + 满 64 行阈值自动移交 ----
    void activity_store_meta_preserved_and_threshold_flush()
    {
        QTemporaryDir base;
        QVERIFY(base.isValid());
        const QString dayDir =
            base.path() + QLatin1Char('/') + QDate::currentDate().toString("yyyy-MM-dd");

        // 场景一：预置当日 meta（summarized:true）→ 写入后不被覆盖（补账进度不丢）
        QVERIFY(QDir().mkpath(dayDir));
        {
            QFile m(dayDir + QStringLiteral("/meta.json"));
            QVERIFY(m.open(QIODevice::WriteOnly));
            m.write("{\"version\":1,\"summarized\":true}");
        }
        {
            ActivityStore store(base.path());
            store.start();
            store.append(QStringLiteral("keys"), QJsonObject{{"ts", 1}});
            store.flushBlocking();
        }
        {
            QFile m(dayDir + QStringLiteral("/meta.json"));
            QVERIFY(m.open(QIODevice::ReadOnly));
            const QJsonObject meta = QJsonDocument::fromJson(m.readAll()).object();
            QCOMPARE(meta.value("summarized").toBool(), true);
            QCOMPARE(meta.value("version").toInt(), 1);
        }

        // 场景二：无 meta → worker 首写自建 summarized:false
        QTemporaryDir base2;
        QVERIFY(base2.isValid());
        const QString dayDir2 =
            base2.path() + QLatin1Char('/') + QDate::currentDate().toString("yyyy-MM-dd");
        {
            ActivityStore store(base2.path());
            store.start();
            store.append(QStringLiteral("keys"), QJsonObject{{"ts", 1}});
            store.flushBlocking();
            QTRY_VERIFY(QFileInfo::exists(dayDir2 + QStringLiteral("/meta.json")));
        }
        {
            QFile m(dayDir2 + QStringLiteral("/meta.json"));
            QVERIFY(m.open(QIODevice::ReadOnly));
            QCOMPARE(QJsonDocument::fromJson(m.readAll()).object()
                         .value("summarized").toBool(), false);
        }

        // 场景三：缓冲满 64 行 → 立即移交 worker（不调 flushBlocking）
        ActivityStore store3(base2.path());
        store3.start();
        for (int i = 0; i < 63; ++i)
            store3.append(QStringLiteral("keys"), QJsonObject{{"i", i}});
        QCOMPARE(store3.bufferedLines(), 63);
        store3.append(QStringLiteral("keys"), QJsonObject{{"i", 63}}); // 第 64 行触发
        QCOMPARE(store3.bufferedLines(), 0);
        store3.stop();
    }

    // ---- Settings：更多非法值回退 / 钳制 / 术语对平行表 ----
    void settings_more_fallbacks_and_clamps()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath("settings_more.ini");
        Settings s(ini);

        // 术语对：空对在写入侧剔除，平行表不串位
        const QList<Settings::TermPair> terms{
            {QStringLiteral("GPU"), QStringLiteral("显卡")},
            {QString(), QStringLiteral("空源")},
            {QStringLiteral("空译"), QString()},
            {QStringLiteral("LLM"), QStringLiteral("大模型")}};
        s.setTranslateTerms(terms);
        const QList<Settings::TermPair> back = s.translateTerms();
        QCOMPARE(back.size(), 2);
        QCOMPARE(back.at(0).first, QStringLiteral("GPU"));
        QCOMPARE(back.at(0).second, QStringLiteral("显卡"));
        QCOMPARE(back.at(1).first, QStringLiteral("LLM"));
        QCOMPARE(back.at(1).second, QStringLiteral("大模型"));
    }

    // ---- ImeCommitDetector：up 事件与非字母非确认键忽略 ----
    void ime_commit_detector_ignores_up_and_other_keys()
    {
        ImeCommitDetector d;
        qint64 clock = 1000000;
        d.setClock([&clock] { return clock; });
        bool cjkIme = true;
        d.setImeGate([&cjkIme] { return cjkIme; });

        int commits = 0;
        connect(&d, &ImeCommitDetector::commitDetected, [&] { ++commits; });

        // 字母的 up 事件不构成组合期证据：随后的空格不触发
        d.onKeyEvent(0x41, false);
        d.onKeyEvent(0x20, true);
        QCOMPARE(commits, 0);

        // 非字母非确认键（F5 等）被忽略：不触发
        d.onKeyEvent(0x74, true); // F5
        d.onKeyEvent(0x20, true);
        QCOMPARE(commits, 0);

        // 对照：字母 down + 确认键 → 正常触发（守卫没有误伤）
        d.onKeyEvent(0x41, true);
        d.onKeyEvent(0x20, true);
        QCOMPARE(commits, 1);
    }

    // ---- ClipboardCodec::looksLikeSecret / maskSecrets：已知前缀命中 ----
    void secret_mask_hits_known_prefixes()
    {
        // 已知前缀 + ≥8 密钥字符 → 命中并整体替换为 [masked]
        QVERIFY(ClipboardCodec::looksLikeSecret(QStringLiteral("sk-abc12345XYZ")));
        QVERIFY(ClipboardCodec::looksLikeSecret(QStringLiteral("ghp_0123456789abcdefghij")));
        QVERIFY(ClipboardCodec::looksLikeSecret(QStringLiteral("AKIAIOSFODNN7EXAMPLE")));
        QVERIFY(ClipboardCodec::looksLikeSecret(QStringLiteral("xoxb-123456789-abcdefghij")));
        QVERIFY(ClipboardCodec::looksLikeSecret(QStringLiteral("eyJhbGciOiJIUzI1NiJ9"))); // JWT 头
        QCOMPARE(ClipboardCodec::maskSecrets(QStringLiteral("key=") + QStringLiteral("sk-abc12345XYZ")),
                 QStringLiteral("key=[masked]"));

        // 前缀后不足 8 个密钥字符 → 不打（提到"sk- 开头的笔记"不误伤）
        QVERIFY(!ClipboardCodec::looksLikeSecret(QStringLiteral("sk-")));
        QVERIFY(!ClipboardCodec::looksLikeSecret(QStringLiteral("sk-ab")));
        QVERIFY(!ClipboardCodec::looksLikeSecret(QStringLiteral("ghp_x")));

        // 泛高熵边界：19 位不打（长度不足）；20 位字母数字混合且去重
        // ≥10 → 打
        const QString nineteen = QStringLiteral("Ab1Cd2Ef3Gh4Ij5Kl6M");
        const QString twenty = QStringLiteral("Ab1Cd2Ef3Gh4Ij5Kl6Mn");
        QCOMPARE(nineteen.size(), 19);
        QCOMPARE(twenty.size(), 20);
        QVERIFY(!ClipboardCodec::looksLikeSecret(nineteen));
        QVERIFY(ClipboardCodec::looksLikeSecret(twenty));

        // Bearer 头：上一 token 为 bearer → 当前 token 打码
        QCOMPARE(ClipboardCodec::maskSecrets(QStringLiteral("Bearer 0123456789abcdefgh")),
                 QStringLiteral("Bearer [masked]"));
    }

    // ---- ClipboardCodec：良性文本不误打（宁可漏打不多打） ----
    void secret_mask_ignores_benign_text()
    {
        QVERIFY(!ClipboardCodec::looksLikeSecret(QStringLiteral("12345678901234567890"))); // 20 纯数字：无字母
        QVERIFY(!ClipboardCodec::looksLikeSecret(QStringLiteral("aaaaaaaaaaaaaaaaaaaa"))); // 去重 <10
        QVERIFY(!ClipboardCodec::looksLikeSecret(QStringLiteral("https://example.com/a/b"))); // 冒号/点号出集
        QVERIFY(!ClipboardCodec::looksLikeSecret(QStringLiteral("2026-11-12T10:30:00Z"))); // 时间戳
        QVERIFY(!ClipboardCodec::looksLikeSecret(QStringLiteral("中文文本不打码")));

        // 整句原样（含提到 sk- 的中文上下文）
        const QString sentence = QStringLiteral("今天把 sk- 开头的笔记整理进双周报了");
        QCOMPARE(ClipboardCodec::maskSecrets(sentence), sentence);

        // 内嵌前缀只在 '=' ':' 引号后认：键值形态打码，普通词不误伤
        QCOMPARE(ClipboardCodec::maskSecrets(QStringLiteral("key=sk-abc12345XYZ")),
                 QStringLiteral("key=[masked]"));
        QCOMPARE(ClipboardCodec::maskSecrets(QStringLiteral("task-12345678")),
                 QStringLiteral("task-12345678")); // "task-" 含 "sk-" 但非边界

        // 短 token / 常规词组不动
        QCOMPARE(ClipboardCodec::maskSecrets(QStringLiteral("hello world 12345")),
                 QStringLiteral("hello world 12345"));
    }

    // ---- ClipboardCodec：emoji（代理对）跨 200 字符边界 + 中文混排密钥
    // token（T-047-FIX(P3-8④) 补两断言：以实际行为断言，不改 codec 行为）----
    void secret_mask_emoji_surrogate_and_cjk_token()
    {
        // 😀 = U+1F600，UTF-16 代理对（2 QChar）。构造 199 字符垫片 + emoji
        //（占 [199,201)，横跨第 200/201 字符边界）+ 内嵌前缀密钥——头/尾
        // 200 字符切片都会把代理对切半，切片与 token 化不得崩溃
        const QString emoji = QStringLiteral("\U0001F600");
        const QString text = QString(199, QLatin1Char('a')) + emoji
                             + QStringLiteral(" key=sk-abc12345XYZ");
        QVERIFY(text.mid(199, 1).at(0).isHighSurrogate());
        QVERIFY(text.mid(200, 1).at(0).isLowSurrogate());

        // 头切片（代理对前半悬在尾）：单 token 无密钥特征 → 原样返回不崩
        const QString head = text.left(200);
        QCOMPARE(ClipboardCodec::maskSecrets(head), head);
        // 尾切片（代理对后半悬在头）：emoji 归入首个 token 原样保留，
        // 密钥 token 经内嵌前缀规则打码
        const QString tail = text.right(200);
        const QString maskedTail = ClipboardCodec::maskSecrets(tail);
        QVERIFY(maskedTail.endsWith(QStringLiteral(" key=[masked]")));
        QVERIFY(!maskedTail.contains(QStringLiteral("sk-abc12345XYZ")));
        // 全文（emoji 完整落在前 token）：前段原样 + 密钥打码
        QCOMPARE(ClipboardCodec::maskSecrets(text),
                 QString(199, QLatin1Char('a')) + emoji
                     + QStringLiteral(" key=[masked]"));
        // 切片入库形态的事件行构造不崩且打码生效
        ClipboardCodec::ClipboardPayload p;
        p.kind = ClipboardCodec::ClipboardPayload::Kind::Text;
        p.chars = text.size();
        p.hash = ClipboardCodec::contentHash(text.toUtf8(), text.size());
        p.head = head;
        p.tail = tail;
        const QJsonObject o = ClipboardCodec::buildClipboardEvent(1, p, {}, {});
        QVERIFY(o.value(QStringLiteral("head")).toString()
                    .startsWith(QString(199, QLatin1Char('a'))));
        QVERIFY(!o.value(QStringLiteral("tail")).toString()
                     .contains(QStringLiteral("sk-abc12345XYZ")));

        // 中文混排密钥 token：密钥字符集外的汉字使整体不判密、也不打码
        //（宁可漏打不多打；"sk-" 位于 token 首位不构成内嵌前缀边界）
        const QString mixed = QStringLiteral("sk-abc12345XYZ密");
        QVERIFY(!ClipboardCodec::looksLikeSecret(mixed));
        QCOMPARE(ClipboardCodec::maskSecrets(QStringLiteral("复制 ") + mixed),
                 QStringLiteral("复制 ") + mixed);
    }

    // ---- ClipboardCodec::buildClipboardEvent：text 事件 JSON 形状 ----
    void clipboard_event_build_text_shape()
    {
        ClipboardCodec::ClipboardPayload p;
        p.kind = ClipboardCodec::ClipboardPayload::Kind::Text;
        p.chars = 15;
        p.hash = QStringLiteral("abcd1234ef567890:15");
        p.head = QStringLiteral("密钥 sk-abc12345XYZ 在这里");
        p.tail = QStringLiteral("尾部预览");
        const QJsonObject o = ClipboardCodec::buildClipboardEvent(
            1234, p, QStringLiteral("code.exe"), QStringLiteral("main.cpp"));

        QCOMPARE(o.value(QStringLiteral("kind")).toString(), QStringLiteral("text"));
        QCOMPARE(o.value(QStringLiteral("ts")).toDouble(), 1234.0);
        QCOMPARE(o.value(QStringLiteral("app")).toString(), QStringLiteral("code.exe"));
        QCOMPARE(o.value(QStringLiteral("title")).toString(), QStringLiteral("main.cpp"));
        QCOMPARE(o.value(QStringLiteral("chars")).toDouble(), 15.0);
        QCOMPARE(o.value(QStringLiteral("hash")).toString(), QStringLiteral("abcd1234ef567890:15"));
        // 预览落盘前已打码：密钥本体不出现在 head，占位符出现
        QVERIFY(o.value(QStringLiteral("head")).toString().contains(QStringLiteral("[masked]")));
        QVERIFY(!o.value(QStringLiteral("head")).toString().contains(QStringLiteral("sk-abc")));
        QCOMPARE(o.value(QStringLiteral("tail")).toString(), QStringLiteral("尾部预览"));
    }

    // ---- ClipboardCodec::buildClipboardEvent：files / image 元数据形状 ----
    void clipboard_event_build_files_and_image()
    {
        // HDROP：路径数组 + 截断标志；图片：只记分辨率与估算字节，无内容
        ClipboardCodec::ClipboardPayload f;
        f.kind = ClipboardCodec::ClipboardPayload::Kind::Files;
        f.paths = QStringList{QStringLiteral("E:/a.txt"), QStringLiteral("E:/b.txt")};
        f.pathsTruncated = true;
        f.hash = QStringLiteral("aaaabbbbccccdddd:7");
        const QJsonObject fo = ClipboardCodec::buildClipboardEvent(5, f, {}, {});
        QCOMPARE(fo.value(QStringLiteral("kind")).toString(), QStringLiteral("files"));
        const QJsonArray paths = fo.value(QStringLiteral("paths")).toArray();
        QCOMPARE(paths.size(), 2);
        QCOMPARE(paths.at(0).toString(), QStringLiteral("E:/a.txt"));
        QCOMPARE(fo.value(QStringLiteral("pathsTruncated")).toBool(), true);
        QVERIFY(!fo.contains(QStringLiteral("head")));

        ClipboardCodec::ClipboardPayload im;
        im.kind = ClipboardCodec::ClipboardPayload::Kind::Image;
        im.width = 1920;
        im.height = 1080;
        im.bytes = 2073600;
        im.hash = QStringLiteral("1111222233334444:2073600");
        const QJsonObject io = ClipboardCodec::buildClipboardEvent(6, im, {}, {});
        QCOMPARE(io.value(QStringLiteral("kind")).toString(), QStringLiteral("image"));
        QCOMPARE(io.value(QStringLiteral("width")).toInt(), 1920);
        QCOMPARE(io.value(QStringLiteral("height")).toInt(), 1080);
        QCOMPARE(io.value(QStringLiteral("bytes")).toDouble(), 2073600.0);
        QVERIFY(!io.contains(QStringLiteral("head")));  // 不落预览/内容
        QVERIFY(!io.contains(QStringLiteral("paths")));
    }

    // ---- ClipboardCodec::ClipboardDeduper：去重与 repeat 冲账时序 ----
    void clipboard_dedup_repeat_transitions()
    {
        ClipboardCodec::ClipboardDeduper d;
        // 首次复制 A：直接落条目，无 repeat 冲账
        auto o1 = d.observe(QStringLiteral("A"));
        QVERIFY(o1.entry);
        QVERIFY(o1.repeatHash.isEmpty());
        // 连续重复 A：不追加新行（只内存累计）
        QVERIFY(!d.observe(QStringLiteral("A")).entry);
        QVERIFY(!d.observe(QStringLiteral("A")).entry);
        // 内容变化 → 先冲账 repeat{A,3}（总次数含首条），再落 B 条目
        auto o4 = d.observe(QStringLiteral("B"));
        QCOMPARE(o4.repeatHash, QStringLiteral("A"));
        QCOMPARE(o4.repeatCount, 3);
        QVERIFY(o4.entry);
        // 无重复的换内容：只落条目
        auto o5 = d.observe(QStringLiteral("C"));
        QVERIFY(o5.repeatHash.isEmpty());
        QVERIFY(o5.entry);
        // B 复制两次后收尾：flush 冲账 repeat{B,2} 并复位
        QVERIFY(d.observe(QStringLiteral("B")).entry);
        QVERIFY(!d.observe(QStringLiteral("B")).entry);
        const auto o6 = d.flush();
        QCOMPARE(o6.repeatHash, QStringLiteral("B"));
        QCOMPARE(o6.repeatCount, 2);
        QVERIFY(!o6.entry);
        QCOMPARE(d.pendingRepeatCount(), 0);
        QVERIFY(d.flush().repeatHash.isEmpty()); // 复位后再 flush：无效
    }

    // ---- UiaTextReader::normalize：换行归一 + 对象符剔除 + 限长取尾 ----
    void uia_text_normalize_and_limit()
    {
        using UTR = UiaTextReader;
        QCOMPARE(UTR::normalize(QStringLiteral("a\r\nb\rc")), QStringLiteral("a\nb\nc"));
        QCOMPARE(UTR::normalize(QStringLiteral("x") + QChar(0xFFFC) + QStringLiteral("y")),
                 QStringLiteral("xy"));
        QCOMPARE(UTR::normalize(QStringLiteral("hi\n")), QStringLiteral("hi"));
        QVERIFY(UTR::normalize(QString()).isEmpty());

        const QString big = QString(UTR::kMaxChars + 500, QLatin1Char('a'))
                            + QStringLiteral("TAIL");
        const QString n = UTR::normalize(big);
        QCOMPARE(n.size(), UTR::kMaxChars);
        QVERIFY(n.endsWith(QStringLiteral("TAIL"))); // 限长保留最新输入（尾部）
    }

    // ---- SnapshotOcrPipeline::diffUiaAdded + 双基线隔离 ----
    void ocr_uia_sliding_diff_and_dual_baseline()
    {
        // 常规三案先走：前缀增长型照旧
        QCOMPARE(SnapshotOcrPipeline::diffUiaAdded(QStringLiteral("ABCDE"),
                                                   QStringLiteral("ABCDEfg")),
                 QStringLiteral("fg"));
        // 滑动窗口：旧窗口 "ABCDE"、光标推进后新窗口 "CDEFG"/"DEFGH"——
        // 旧文本非前缀/后缀/子串，靠最长后缀=前缀补解
        QCOMPARE(SnapshotOcrPipeline::diffUiaAdded(QStringLiteral("ABCDE"),
                                                   QStringLiteral("CDEFG")),
                 QStringLiteral("FG"));
        QCOMPARE(SnapshotOcrPipeline::diffUiaAdded(QStringLiteral("ABCDE"),
                                                   QStringLiteral("DEFGH")),
                 QStringLiteral("FGH"));
        // 无重叠（版面大改）→ 宁缺毋滥返回空
        QVERIFY(SnapshotOcrPipeline::diffUiaAdded(QStringLiteral("ABCDE"),
                                                  QStringLiteral("XYZUV"))
                    .isEmpty());

        // 双基线隔离：同一 hwnd 在 OCR 基线与 UIA 基线各自独立推进，
        // 互不把对方全量文本当增量
        QHash<quintptr, QString> ocrBase, uiaBase;
        SnapshotOcrPipeline::consumeBaselineAdded(ocrBase, 9, QStringLiteral("ocrfull"));
        const QString uiaAdded = SnapshotOcrPipeline::consumeBaselineAdded(
            uiaBase, 9, QStringLiteral("uiafulltext"),
            &SnapshotOcrPipeline::diffUiaAdded);
        QCOMPARE(uiaAdded, QStringLiteral("uiafulltext")); // UIA 基线首拍全量
        QCOMPARE(uiaBase.value(9), QStringLiteral("uiafulltext"));
        QVERIFY(ocrBase.value(9) != uiaBase.value(9));     // 两基线互不污染
        // UIA 基线二拍：增量正确
        QCOMPARE(SnapshotOcrPipeline::consumeBaselineAdded(
                     uiaBase, 9, QStringLiteral("uiafulltext+more"),
                     &SnapshotOcrPipeline::diffUiaAdded),
                 QStringLiteral("+more"));
    }

    // ---- TextMirrorStore：双向差分 + 字段影子（T-066 文本镜像） ----
    void text_mirror_store_diff_and_shadows()
    {
        using TM = TextMirrorStore;
        // 纯差分：打字增长 / 回退 / 中段替换 / 无变化 / 空基线
        auto c = TM::diffChange(QStringLiteral("hello"), QStringLiteral("hello world"));
        QVERIFY(c.removed.isEmpty());
        QCOMPARE(c.added, QStringLiteral(" world"));
        c = TM::diffChange(QStringLiteral("hello world"), QStringLiteral("hello"));
        QCOMPARE(c.removed, QStringLiteral(" world"));
        QVERIFY(c.added.isEmpty());
        c = TM::diffChange(QStringLiteral("hello world"), QStringLiteral("hello World"));
        QCOMPARE(c.removed, QStringLiteral("w"));
        QCOMPARE(c.added, QStringLiteral("W"));
        c = TM::diffChange(QStringLiteral("同"), QStringLiteral("同"));
        QVERIFY(c.removed.isEmpty() && c.added.isEmpty());
        c = TM::diffChange(QString(), QStringLiteral("你好"));
        QVERIFY(c.removed.isEmpty());
        QCOMPARE(c.added, QStringLiteral("你好"));

        // 字段身份键退化链：RuntimeId → 元素句柄 → 顶层窗口；窗口身份键：
        // 顶层窗口 → 元素进程号 → 空
        UiaTextReader::Result r;
        r.runtimeId = QStringLiteral("42.7");
        r.elHwnd = 0xAB;
        r.topHwnd = 0x10;
        r.elPid = 7;
        QCOMPARE(TextMirrorMonitor::fieldKeyOf(r), QStringLiteral("r:42.7"));
        QCOMPARE(TextMirrorMonitor::windowKeyOf(r), QStringLiteral("w:10"));
        r.runtimeId.clear();
        QCOMPARE(TextMirrorMonitor::fieldKeyOf(r), QStringLiteral("h:ab"));
        r.elHwnd = 0;
        QCOMPARE(TextMirrorMonitor::fieldKeyOf(r), QStringLiteral("t:10"));
        r.topHwnd = 0;
        QCOMPARE(TextMirrorMonitor::windowKeyOf(r), QStringLiteral("p:7"));
        r.elPid = 0;
        QVERIFY(TextMirrorMonitor::windowKeyOf(r).isEmpty());

        // 影子消费（既有身份）：首拍只建影子，二拍起产增删事件（delete
        // 携带被删原文）
        TM s;
        const QString field = QStringLiteral("r:1.2.3");
        QVERIFY(s.applyRead(field, QStringLiteral("w:1"), QStringLiteral("hello")).isEmpty());
        const auto evs = s.applyRead(field, QStringLiteral("w:1"),
                                     QStringLiteral("hello world"));
        QCOMPARE(evs.size(), 1);
        QCOMPARE(evs.front().kind, QStringLiteral("insert"));
        QCOMPARE(evs.front().text, QStringLiteral(" world"));
        QVERIFY(evs.front().removed.isEmpty());
        QCOMPARE(evs.front().field, field);
        const auto evs2 = s.applyRead(field, QStringLiteral("w:1"), QStringLiteral("hello"));
        QCOMPARE(evs2.size(), 1);
        QCOMPARE(evs2.front().kind, QStringLiteral("delete"));
        QCOMPARE(evs2.front().removed, QStringLiteral(" world"));

        // churn 护栏：两侧齐变合计超阈值判为窗口滑动伪变化（宁缺毋滥丢事件、
        // 基线照常推进）；纯增长不受护栏限制（大段粘贴必须抄全）。f2 与 field
        // 同窗口但内容毫无重叠（重叠 0 < 8），续接不采信 → 静默建影子
        const QString big1 = QString(400, QLatin1Char('a'));
        const QString big2 = QString(400, QLatin1Char('b'));
        QVERIFY(s.applyRead(QStringLiteral("f2"), QStringLiteral("w:1"), big1).isEmpty());
        QVERIFY(s.applyRead(QStringLiteral("f2"), QStringLiteral("w:1"), big2).isEmpty()); // 400+400 > 600 丢弃
        QVERIFY(s.applyRead(QStringLiteral("f2"), QStringLiteral("w:1"), big2).isEmpty()); // 稳态无事件
        const auto evs3 = s.applyRead(field, QStringLiteral("w:1"),
                                      QStringLiteral("hello world")
                                          + QString(800, QLatin1Char('p')));
        QCOMPARE(evs3.size(), 1);
        QCOMPARE(evs3.front().kind, QStringLiteral("insert"));

        // 密码字段影子重置（独立窗口免续接干扰）：dropShadow 后再读按首拍
        // 处理，密码框现状不会以增量形式带出
        s.applyRead(QStringLiteral("pwd"), QStringLiteral("w:2"), QStringLiteral("secret-1"));
        s.dropShadow(QStringLiteral("pwd"));
        QVERIFY(s.applyRead(QStringLiteral("pwd"), QStringLiteral("w:2"), QStringLiteral("secret-2")).isEmpty());

        // LRU 上限：影子数恒 ≤ kMaxFieldShadows；k 系列同窗口但重叠 1 < 8
        // 不续接，全部静默
        for (int i = 0; i < 6; ++i)
            QVERIFY(s.applyRead(QStringLiteral("k%1").arg(i), QStringLiteral("w:9"),
                                QStringLiteral("x")).isEmpty());
        QVERIFY(s.shadowCount() <= TM::kMaxFieldShadows);
    }

    // ---- TextMirrorStore：同窗口续接（UI 重建不吞增量，T-066 验收期修复） ----
    void text_mirror_store_continuation()
    {
        using TM = TextMirrorStore;
        TM s;
        const QString old = QStringLiteral("今天讨论第五章的内容安排");
        QVERIFY(s.applyRead(QStringLiteral("a1"), QStringLiteral("w:c"), old).isEmpty()); // 首拍

        // UI 重建：同窗口新身份、内容小步前进 → 续接补记，field=新身份
        const auto evs = s.applyRead(QStringLiteral("a2"), QStringLiteral("w:c"),
                                     old + QStringLiteral("补充"));
        QCOMPARE(evs.size(), 1);
        QCOMPARE(evs.front().kind, QStringLiteral("insert"));
        QCOMPARE(evs.front().text, QStringLiteral("补充"));
        QCOMPARE(evs.front().field, QStringLiteral("a2"));

        // 重建后内容未变：迁基线不产事件
        QVERIFY(s.applyRead(QStringLiteral("a3"), QStringLiteral("w:c"),
                            old + QStringLiteral("补充")).isEmpty());

        // 重建 + 编辑：续接差分产 replace（改字）
        const auto evs2 = s.applyRead(QStringLiteral("a4"), QStringLiteral("w:c"),
                                      QString(old + QStringLiteral("补充"))
                                          .replace(QStringLiteral("五章"),
                                                   QStringLiteral("四章")));
        QCOMPARE(evs2.size(), 1);
        QCOMPARE(evs2.front().kind, QStringLiteral("replace"));
        QCOMPARE(evs2.front().removed, QStringLiteral("五"));
        QCOMPARE(evs2.front().text, QStringLiteral("四"));

        // 跨窗口不续接：陌生窗口静默建影子
        QVERIFY(s.applyRead(QStringLiteral("b1"), QStringLiteral("w:other"),
                            QStringLiteral("完全不同的一段文字内容")).isEmpty());

        // 重叠不足（2 < 8）不续接：短内容静默
        QVERIFY(s.applyRead(QStringLiteral("c1"), QStringLiteral("w:c"),
                            QStringLiteral("补充")).isEmpty());

        // 窗口身份缺失不续接
        QVERIFY(s.applyRead(QStringLiteral("d1"), QString(), old).isEmpty());
    }

    // ---- OwnWriteGuard：自身写入 seq 记录与排除 ----
    void own_write_guard_seq_membership()
    {
        OwnWriteGuard::reset();
        QVERIFY(!OwnWriteGuard::isSelfWrite(0)); // 0 恒为"非自身"（未写剪贴板）
        QVERIFY(!OwnWriteGuard::isSelfWrite(-7));
#ifdef Q_OS_WIN
        // noteSelfWrite 记录当前会话序列号；期间若有并发写入序列号只会
        // 单调前移，故检查 s1..s1+3 窗口必有命中
        const qint64 s1 = static_cast<qint64>(GetClipboardSequenceNumber());
        OwnWriteGuard::noteSelfWrite();
        bool hit = false;
        for (qint64 s = s1; s <= s1 + 3 && !hit; ++s)
            hit = OwnWriteGuard::isSelfWrite(s);
        QVERIFY(hit);
        // reset 后排除生效（记录被清空）
        OwnWriteGuard::reset();
        QVERIFY(!OwnWriteGuard::isSelfWrite(s1));
#endif
    }

    // ============================================================
    // T-054：桌面日历（照搬 rlqd 复刻）——农历引擎 / 布局 / 存储 / 设置
    // ============================================================

    // 节日锚点（solarlunar 权威库逐日真值校验，见 temp_scripts/script_gen_lunar_truth.js）
    void chinese_calendar_festival_anchors()
    {
        QCOMPARE(ChineseCalendar::festival(QDate(2020, 1, 25)), QStringLiteral("春节"));
        QCOMPARE(ChineseCalendar::festival(QDate(2024, 2, 10)), QStringLiteral("春节"));
        QCOMPARE(ChineseCalendar::festival(QDate(2025, 1, 29)), QStringLiteral("春节"));
        QCOMPARE(ChineseCalendar::festival(QDate(2026, 2, 17)), QStringLiteral("春节"));
        QCOMPARE(ChineseCalendar::festival(QDate(2027, 2, 6)), QStringLiteral("春节"));
        QCOMPARE(ChineseCalendar::festival(QDate(2028, 1, 26)), QStringLiteral("春节"));
        QCOMPARE(ChineseCalendar::festival(QDate(2025, 10, 6)), QStringLiteral("中秋节"));
        QCOMPARE(ChineseCalendar::festival(QDate(2026, 9, 25)), QStringLiteral("中秋节"));
        QCOMPARE(ChineseCalendar::festival(QDate(2024, 6, 10)), QStringLiteral("端午节"));
        QCOMPARE(ChineseCalendar::festival(QDate(2025, 5, 31)), QStringLiteral("端午节"));
        QCOMPARE(ChineseCalendar::festival(QDate(2026, 6, 19)), QStringLiteral("端午节"));
        QCOMPARE(ChineseCalendar::festival(QDate(2025, 2, 12)), QStringLiteral("元宵节"));
        QCOMPARE(ChineseCalendar::festival(QDate(2025, 1, 28)), QStringLiteral("除夕"));
        QCOMPARE(ChineseCalendar::festival(QDate(2026, 2, 16)), QStringLiteral("除夕"));
        QCOMPARE(ChineseCalendar::festival(QDate(2026, 10, 1)), QStringLiteral("国庆节"));
        QCOMPARE(ChineseCalendar::festival(QDate(2026, 12, 25)), QStringLiteral("圣诞夜"));
        QCOMPARE(ChineseCalendar::festival(QDate(2026, 11, 16)), QString());
    }

    void chinese_calendar_lunar_dates_and_labels()
    {
        int y = 0, m = 0, d = 0;
        QVERIFY(ChineseCalendar::getLunar(QDate(2026, 11, 16), &y, &m, &d));
        QCOMPARE(y, 2026);
        QCOMPARE(m, 10);
        QCOMPARE(d, 8);
        QCOMPARE(ChineseCalendar::lunarMonthName(QDate(2026, 11, 16)), QStringLiteral("十月"));
        QCOMPARE(ChineseCalendar::lunarDayName(QDate(2026, 11, 16)), QStringLiteral("初八"));
        // 闰月真值：2020 闰四月 / 2023 闰二月 / 2025 闰六月
        QVERIFY(ChineseCalendar::isLeapLunarMonth(QDate(2020, 5, 23)));
        QCOMPARE(ChineseCalendar::lunarMonthName(QDate(2020, 5, 23)), QStringLiteral("闰四月"));
        QCOMPARE(ChineseCalendar::lunarDayName(QDate(2020, 5, 23)), QStringLiteral("初一"));
        QVERIFY(ChineseCalendar::isLeapLunarMonth(QDate(2023, 3, 22)));
        QCOMPARE(ChineseCalendar::lunarMonthName(QDate(2023, 3, 22)), QStringLiteral("闰二月"));
        QVERIFY(ChineseCalendar::isLeapLunarMonth(QDate(2025, 7, 25)));
        QCOMPARE(ChineseCalendar::lunarMonthName(QDate(2025, 7, 25)), QStringLiteral("闰六月"));
        QCOMPARE(ChineseCalendar::lunarDayName(QDate(2025, 7, 25)), QStringLiteral("初一"));
        // 月初标签（LunarMonth 类；2026-02-17 是春节走 Holiday 分支，取十一月月初）
        const DayLabel label = ChineseCalendar::dayLabel(QDate(2026, 12, 9), true);
        QCOMPARE(label.kind, DayLabelKind::LunarMonth);
        QCOMPARE(label.text, QStringLiteral("农历冬月"));
        // 节气优先于节日/农历
        const DayLabel termLabel = ChineseCalendar::dayLabel(QDate(2026, 11, 7), true);
        QCOMPARE(termLabel.kind, DayLabelKind::SolarTerm);
        QCOMPARE(termLabel.text, QStringLiteral("立冬"));
        const DayLabel festLabel = ChineseCalendar::dayLabel(QDate(2026, 10, 1), true);
        QCOMPARE(festLabel.kind, DayLabelKind::Holiday);
        QCOMPARE(festLabel.text, QStringLiteral("国庆节"));
        // showLunar=false 时普通日不产农历文案
        const DayLabel plain = ChineseCalendar::dayLabel(QDate(2026, 11, 20), false);
        QVERIFY(plain.text.isEmpty());
        // 出表日期不崩溃且返回空
        int y2 = 0, m2 = 0, d2 = 0;
        QVERIFY(!ChineseCalendar::getLunar(QDate(1899, 12, 31), &y2, &m2, &d2));
        QCOMPARE(y2, 0);
    }

    void chinese_calendar_solar_terms_2026()
    {
        // rlqd 同款分钟级近似公式在 2026 年的 24 个节气（与 C# 公式逐日比对）
        const QVector<QPair<QDate, QString>> expected = {
            { QDate(2026, 1, 5), QStringLiteral("小寒") },
            { QDate(2026, 1, 20), QStringLiteral("大寒") },
            { QDate(2026, 2, 4), QStringLiteral("立春") },
            { QDate(2026, 2, 18), QStringLiteral("雨水") },
            { QDate(2026, 3, 5), QStringLiteral("惊蛰") },
            { QDate(2026, 3, 20), QStringLiteral("春分") },
            { QDate(2026, 4, 5), QStringLiteral("清明") },
            { QDate(2026, 4, 20), QStringLiteral("谷雨") },
            { QDate(2026, 5, 5), QStringLiteral("立夏") },
            { QDate(2026, 5, 21), QStringLiteral("小满") },
            { QDate(2026, 6, 6), QStringLiteral("芒种") },
            { QDate(2026, 6, 21), QStringLiteral("夏至") },
            { QDate(2026, 7, 7), QStringLiteral("小暑") },
            { QDate(2026, 7, 23), QStringLiteral("大暑") },
            { QDate(2026, 8, 7), QStringLiteral("立秋") },
            { QDate(2026, 8, 23), QStringLiteral("处暑") },
            { QDate(2026, 9, 7), QStringLiteral("白露") },
            { QDate(2026, 9, 23), QStringLiteral("秋分") },
            { QDate(2026, 10, 8), QStringLiteral("寒露") },
            { QDate(2026, 10, 23), QStringLiteral("霜降") },
            { QDate(2026, 11, 7), QStringLiteral("立冬") },
            { QDate(2026, 11, 22), QStringLiteral("小雪") },
            { QDate(2026, 12, 7), QStringLiteral("大雪") },
            { QDate(2026, 12, 22), QStringLiteral("冬至") }
        };
        for (const auto &e : expected)
            QCOMPARE(ChineseCalendar::solarTerm(e.first), e.second);
        // 非节气日为空
        QVERIFY(ChineseCalendar::solarTerm(QDate(2026, 11, 16)).isEmpty());
    }

    void chinese_calendar_iso_week_number()
    {
        // ISO 8601：2026 W1 起于 2025-12-29（周一）；2026-11-16 在第 47 周
        QCOMPARE(ChineseCalendar::weekNumber(QDate(2025, 12, 29)), 1);
        QCOMPARE(ChineseCalendar::weekNumber(QDate(2026, 1, 1)), 1);
        QCOMPARE(ChineseCalendar::weekNumber(QDate(2026, 11, 16)), 47);
        QCOMPARE(ChineseCalendar::weekNumber(QDate(2024, 12, 30)), 1); // 2025 W1
        QCOMPARE(ChineseCalendar::weekNumber(QDate(2021, 1, 1)), 53);  // 2020 W53
    }

    void calendar_layout_monday_start_and_hit_test()
    {
        // 2026-11-01 是周日 → skipPartial（首周只有一天则跳过），首格 11-02（周一）
        const CalendarLayout layout(QRect(640, 20, 1260, 795), 2026, 11);
        QCOMPARE(layout.firstCellDate, QDate(2026, 11, 2));
        QCOMPARE(layout.rows, 5);
        QCOMPARE(layout.cellDate(0, 0), QDate(2026, 11, 2));
        QCOMPARE(layout.cellDate(2, 0), QDate(2026, 11, 16));
        QCOMPARE(layout.inDisplayMonth(QDate(2026, 11, 16)), true);
        QCOMPARE(layout.inDisplayMonth(QDate(2026, 10, 31)), false);
        // 命中测试：格内命中、头部按钮命中、格外不命中
        int row = 0, col = 0;
        QDate date;
        QRect cell;
        const QRect c21 = layout.cellRect(2, 1);
        QVERIFY(layout.tryGetCell(c21.center(), &row, &col, &date, &cell));
        QCOMPARE(row, 2);
        QCOMPARE(col, 1);
        QCOMPARE(date, QDate(2026, 11, 17));
        QCOMPARE(layout.tryGetCell(QPoint(-500, -500), &row, &col, &date, &cell), false);
        QVERIFY(!layout.headerButtons.isEmpty());
        QCOMPARE(layout.headerButtons.size(), 3); // T-056 后 3 按钮（翻页+调整，其余入设置页）
        const int hit = layout.hitHeader(layout.headerButtons.at(2).center());
        QCOMPARE(hit, 2);
        QCOMPARE(layout.hitHeader(QPoint(-1, -1)), -1);
        // 周日开月的 skipPartial：2026-02-01 是周日 → 首格 02-02
        const CalendarLayout feb(QRect(0, 0, 1260, 795), 2026, 2);
        QCOMPARE(feb.firstCellDate, QDate(2026, 2, 2));
    }

    void calendar_task_store_roundtrip_and_legacy()
    {
        // 数据目录重定向到临时目录，绝不写真实用户数据
        QTemporaryDir dataDir;
        QVERIFY(dataDir.isValid());
        CalendarSettings::setDataDirOverride(dataDir.path());
        // 新格式 JSON 往返
        {
            CalendarTaskStore store;
            QVector<CalendarTaskItem> tasks;
            tasks.append({ QStringLiteral("写周报"), false, QStringLiteral("09:00") });
            tasks.append({ QStringLiteral("已完成的待办"), true, {} });
            store.set(QDate(2026, 11, 16), tasks);
            const QString json = store.exportJson();
            CalendarTaskStore parsed;
            QVERIFY(parsed.fromJson(json));
            const QVector<CalendarTaskItem> back = parsed.get(QDate(2026, 11, 16));
            QCOMPARE(back.size(), 2);
            QCOMPARE(back.at(0).text, QStringLiteral("写周报"));
            QCOMPARE(back.at(0).time, QStringLiteral("09:00"));
            QCOMPARE(back.at(1).done, true);
        }
        // 空列表删除键
        {
            CalendarTaskStore store;
            store.set(QDate(2026, 11, 16), { { QStringLiteral("x"), false, {} } });
            QCOMPARE(store.hasTasks(QDate(2026, 11, 16)), true);
            store.set(QDate(2026, 11, 16), {});
            QCOMPARE(store.hasTasks(QDate(2026, 11, 16)), false);
        }
        // 旧格式（DesktopCal 导出 {"All":[...]}）兼容
        {
            CalendarTaskStore store;
            QVERIFY(store.fromJson(QStringLiteral(
                "{\"All\":[{\"Key\":\"2026-11-18\",\"Value\":[{\"Text\":\"旧格式任务\","
                "\"Done\":false,\"Time\":null}]}]}")));
            const QVector<CalendarTaskItem> tasks = store.get(QDate(2026, 11, 18));
            QCOMPARE(tasks.size(), 1);
            QCOMPARE(tasks.at(0).text, QStringLiteral("旧格式任务"));
        }
        // 非日期键被忽略；非法 JSON 拒绝
        {
            CalendarTaskStore store;
            QVERIFY(store.fromJson(QStringLiteral("{\"hello\":[],\"2026-11-18\":[]}")));
            QCOMPARE(store.hasTasks(QDate(2026, 11, 18)), false);
            QVERIFY(!store.fromJson(QStringLiteral("not json")));
            QVERIFY(!store.fromJson(QStringLiteral("[1,2]")));
        }
        // 合并
        {
            CalendarTaskStore base;
            base.set(QDate(2026, 11, 18), { { QStringLiteral("原任务"), false, {} } });
            CalendarTaskStore patch;
            patch.set(QDate(2026, 11, 19), { { QStringLiteral("新任务"), false, {} } });
            base.mergeFrom(patch);
            QCOMPARE(base.get(QDate(2026, 11, 18)).size(), 1);
            QCOMPARE(base.get(QDate(2026, 11, 19)).size(), 1);
        }
        // 落盘往返（重定向下 settings.json / tasks.json 真实读写）
        {
            CalendarTaskStore store;
            store.set(QDate(2026, 11, 20), { { QStringLiteral("落盘任务"), false, {} } });
            CalendarTaskStore reloaded = CalendarTaskStore::load();
            QCOMPARE(reloaded.get(QDate(2026, 11, 20)).size(), 1);
            QCOMPARE(reloaded.get(QDate(2026, 11, 20)).at(0).text, QStringLiteral("落盘任务"));
            CalendarSettings saved;
            saved.x = 42;
            saved.save();
            QCOMPARE(CalendarSettings::load().x, 42);
        }
        CalendarSettings::setDataDirOverride(QString());
    }

    void calendar_settings_roundtrip()
    {
        CalendarSettings s;
        QCOMPARE(s.x, 640);
        QCOMPARE(s.y, 20);
        QCOMPARE(s.width, 1260);
        QCOMPARE(s.height, 795);
        QCOMPARE(s.glassEffect, true);
        QCOMPARE(s.fontScale, 100);
        s.x = 100;
        s.y = 200;
        s.width = 800;
        s.height = 600;
        s.hidden = true;
        s.showLunar = false;
        s.showWeekNumbers = false;
        s.showEvents = false;
        s.glassEffect = false;
        s.fontScale = 135;
        s.showGuide = false;
        s.showNextMonthPill = false;
        const QJsonObject o = s.toJson();
        CalendarSettings back;
        back.fromJson(o);
        QCOMPARE(back.x, 100);
        QCOMPARE(back.y, 200);
        QCOMPARE(back.width, 800);
        QCOMPARE(back.height, 600);
        QCOMPARE(back.hidden, true);
        QCOMPARE(back.showLunar, false);
        QCOMPARE(back.fontScale, 135);
        QCOMPARE(back.rect(), QRect(100, 200, 800, 600));
    }

    void box_layout_sizing_snap_pagination_hit()
    {
        // 尺寸纪律：面板宽 = cols×kCellW 整数倍；单页高 = 头部 + rows×kCellH
        const QRect screen(0, 0, 1920, 1080);
        const auto single = BoxLayout::compute(QPoint(1400, 220), 4, 12, screen);
        QCOMPARE(single.rows, 3);            // 12 项 ÷ 4 列 = 3 行
        QCOMPARE(single.pageCount, 1);       // 恰好一页
        QCOMPARE(single.panel.width(), 4 * BoxLayout::kCellW);
        QCOMPARE(single.panel.height(),
                 BoxLayout::kHeaderHeight + 3 * BoxLayout::kCellH); // 无页圆点条

        // 自动增高：条目变多 → 行数增加但不超过屏；超容量翻页（高度含圆点条）
        const auto paged = BoxLayout::compute(QPoint(1400, 220), 4, 40, screen);
        QVERIFY(paged.rows < (40 + 3) / 4);              // 行数被屏幕钳制
        QVERIFY(paged.pageCount >= 2);
        QCOMPARE(paged.panel.height(), BoxLayout::kHeaderHeight
                                           + paged.rows * BoxLayout::kCellH
                                           + BoxLayout::kDotsHeight);

        // 逐项布局：构造与命中
        const BoxLayout layout(paged.panel, 4, 40, 0);
        QCOMPARE(layout.cols, 4);
        QCOMPARE(layout.capacity, layout.cols * layout.rows);
        QCOMPARE(layout.pageCountFor(40, layout.capacity),
                 40 % layout.capacity == 0 ? 40 / layout.capacity : 40 / layout.capacity + 1);
        QCOMPARE(layout.page, 0); // 构造钳制
        QCOMPARE(layout.headerButtons.size(), 3);
        QVERIFY(layout.headerButtons.at(0).left() < layout.headerButtons.at(2).left());
        QCOMPARE(layout.hitHeader(layout.headerButtons.at(1).center()), 1);
        QCOMPARE(layout.hitHeader(QPoint(-1, -1)), -1);
        QCOMPARE(layout.entryIndexAt(layout.cells.at(0).center()), 0);
        QCOMPARE(layout.entryIndexAt(layout.cells.at(layout.capacity - 1).center()),
                 layout.capacity - 1);
        int cell = -1;
        QCOMPARE(layout.hitCell(layout.cells.at(5).center(), &cell), 5);
        QCOMPARE(cell, 5);
        // 空盒：全格无效命中
        const auto emptySz = BoxLayout::compute(QPoint(0, 0), 4, 0, screen);
        const BoxLayout empty(emptySz.panel, 4, 0, 0);
        QCOMPARE(empty.entryIndexAt(empty.cells.at(0).center()), -1);
        QCOMPARE(empty.pageCount, 1);
        // 页钳制 + 圆点可见性（≤9 页画圆点，>9 页画 n/m 文本）
        const BoxLayout clamped(paged.panel, 4, 40, 99);
        QCOMPARE(clamped.page, clamped.pageCount - 1);
        QCOMPARE(clamped.dots.isEmpty(), clamped.pageCount > 9);

        // 拖拽吸附：任意宽度/高度 → 列/行整数（2~8 列）
        QCOMPARE(BoxLayout::snapCols(180), 2);
        QCOMPARE(BoxLayout::snapCols(300), 3);
        QCOMPARE(BoxLayout::snapCols(8 * BoxLayout::kCellW), 8);
        QCOMPARE(BoxLayout::snapCols(9 * BoxLayout::kCellW + 40), 8); // 钳上限
        QCOMPARE(BoxLayout::snapCols(30), 2);                         // 钳下限
        const int rows3 = BoxLayout::snapRows(BoxLayout::kHeaderHeight
                                                  + 3 * BoxLayout::kCellH
                                                  + BoxLayout::kDotsHeight,
                                              true);
        QCOMPARE(rows3, 3);
        const QRect snapped = BoxLayout::snappedRect(QPoint(100, 100), 5, 2, false);
        QCOMPARE(snapped.width(), 5 * BoxLayout::kCellW);
        QCOMPARE(snapped.height(),
                 BoxLayout::kHeaderHeight + 2 * BoxLayout::kCellH); // 无页 → 无圆点条
    }

    void box_catalog_roundtrip_ops_and_bad_file()
    {
        // 数据目录重定向到临时目录，绝不写真实用户数据
        QTemporaryDir dataDir;
        QVERIFY(dataDir.isValid());
        BoxCatalog::setDataDirOverride(dataDir.path());
        // 多盒增改 + 落盘往返
        QString idA, idB;
        {
            BoxCatalog catalog;
            BoxDef a;
            a.name = QStringLiteral("工具");
            a.dir = QStringLiteral("C:\\Users\\x\\Documents\\工具");
            a.x = 1400;
            a.y = 220;
            a.cols = 4;
            idA = catalog.addBox(a);
            BoxDef b;
            b.name = QStringLiteral("下载");
            b.dir = QDir::toNativeSeparators(dataDir.path());
            idB = catalog.addBox(b);
            QVERIFY(!idA.isEmpty() && !idB.isEmpty() && idA != idB);
            catalog.renameBox(idA, QStringLiteral("常用工具"));
            BoxDef updated = a;
            updated.name = QStringLiteral("常用工具");
            updated.x = 1500;
            updated.page = 1;
            updated.cols = 9; // 越界列数入库时钳制
            catalog.updateBox(idA, updated);
            catalog.setHiddenAll(true);
            QCOMPARE(catalog.boxes().size(), 2);
            QCOMPARE(catalog.indexOf(idB), 1);
        }
        {
            const BoxCatalog reloaded = BoxCatalog::load();
            QCOMPARE(reloaded.boxes().size(), 2);
            const BoxDef &a = reloaded.boxes().at(0);
            QCOMPARE(a.id, idA);
            QCOMPARE(a.name, QStringLiteral("常用工具"));
            QCOMPARE(a.x, 1500);
            QCOMPARE(a.page, 1);
            QCOMPARE(a.cols, 8);        // 钳制生效
            QCOMPARE(a.hidden, true);
            QVERIFY(QDir::fromNativeSeparators(a.dir).endsWith(
                QDir::fromNativeSeparators(QStringLiteral("Documents/工具"))));
            const BoxDef &b = reloaded.boxes().at(1);
            QCOMPARE(b.id, idB);
            QCOMPARE(b.dir, QDir::toNativeSeparators(dataDir.path()));
            // 解除绑定不删文件夹（文件夹门户语义核心红线）
            BoxCatalog mutableReload = reloaded;
            mutableReload.removeBox(idB);
            QCOMPARE(mutableReload.boxes().size(), 1);
            QVERIFY(QDir(dataDir.path()).exists()); // 文件夹仍在
        }
        // 托管目录重名顺延（-2），绝不并目录
        {
            const QString base = BoxCatalog::managedDirFor(QStringLiteral("收藏"));
            QVERIFY(QDir().mkpath(base));
            const QString second = BoxCatalog::managedDirFor(QStringLiteral("收藏"));
            QVERIFY(second != base);
            QVERIFY(second.endsWith(QStringLiteral("-2")));
        }
        // 坏档自愈：.bad 留证 + 空载继续（防坏档反复崩溃）
        {
            QFile bad(BoxCatalog::storePath());
            QVERIFY(bad.open(QIODevice::WriteOnly | QIODevice::Truncate));
            bad.write("{ broken json !!");
            bad.close();
            const BoxCatalog recovered = BoxCatalog::load();
            QCOMPARE(recovered.boxes().size(), 0);
            QVERIFY(QFileInfo::exists(BoxCatalog::storePath() + QStringLiteral(".bad")));
            BoxCatalog parser;
            QCOMPARE(parser.fromJson(QStringLiteral("not json")), false);
        }
        BoxCatalog::setDataDirOverride(QString());
    }

    // ---- MCP 能力桥工具（T-074 M2）：活动数据读取 / 日报月报落盘往返 ----
    void mcp_activity_daily_streams_and_whitelist()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString monitor = dir.filePath("monitor");
        const QString date = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
        QVERIFY(QDir(monitor + "/" + date).mkpath("."));
        {
            QFile f(monitor + "/" + date + "/timeline.jsonl");
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write("{\"app\":\"editor\",\"secs\":120}\n");
            f.write("{\"app\":\"browser\",\"secs\":300}\n");
            f.close();
        }
        // 单流读取
        poptools::McpToolResult r = poptools::runActivityDaily(
            monitor, QJsonObject{{"stream", QStringLiteral("timeline")}, {"date", date}});
        QCOMPARE(r.isError, false);
        QCOMPARE(r.payload.value("ok").toBool(), true);
        QCOMPARE(r.payload.value("streams").toObject().value("timeline")
                     .toObject().value("lines").toArray().size(), 2);
        // 白名单外拒绝（防路径穿越）
        r = poptools::runActivityDaily(
            monitor, QJsonObject{{"stream", QStringLiteral("../secret")}});
        QCOMPARE(r.isError, true);
        // 日期格式非法拒绝
        r = poptools::runActivityDaily(
            monitor, QJsonObject{{"date", QStringLiteral("../evil")}});
        QCOMPARE(r.isError, true);
    }

    void mcp_digest_daily_monthly_roundtrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString goody = dir.filePath("goody");

        // 日报写入 → 读取往返；日历约定路径核对（goody/daily/<date>.md）
        const QString date = QStringLiteral("2026-11-21");
        poptools::McpToolResult r = poptools::runWriteDailyDigest(
            goody, QJsonObject{{"date", date},
                               {"markdown", QStringLiteral("# 日报\n写了代码")}});
        QCOMPARE(r.isError, false);
        const QString written = r.payload.value("path").toString();
        QVERIFY(written.endsWith(QStringLiteral("/daily/2026-11-21.md"))); // QDir::filePath 恒正斜杠
        r = poptools::runReadDailyDigest(goody, QJsonObject{{"date", date}});
        QCOMPARE(r.isError, false);
        QCOMPARE(r.payload.value("exists").toBool(), true);
        QCOMPARE(r.payload.value("markdown").toString().contains(QStringLiteral("写了代码")), true);

        // 未写日期 → exists=false（合法结果非错误）
        r = poptools::runReadDailyDigest(goody, QJsonObject{{"date", QStringLiteral("2026-11-22")}});
        QCOMPARE(r.isError, false);
        QCOMPARE(r.payload.value("exists").toBool(), false);

        // 月报写入 → 读取往返（goody/monthly/<yyyy-MM>.md）
        r = poptools::runWriteMonthlyDigest(
            goody, QJsonObject{{"year", 2026}, {"month", 11},
                               {"markdown", QStringLiteral("# 11 月月报")}});
        QCOMPARE(r.isError, false);
        r = poptools::runReadMonthlyDigest(goody, QJsonObject{{"year", 2026}, {"month", 11}});
        QCOMPARE(r.isError, false);
        QCOMPARE(r.payload.value("markdown").toString().contains(QStringLiteral("月报")), true);

        // 非法月份拒绝
        r = poptools::runWriteMonthlyDigest(
            goody, QJsonObject{{"year", 2026}, {"month", 13}, {"markdown", QStringLiteral("x")}});
        QCOMPARE(r.isError, true);
    }
};


QTEST_MAIN(DomainTests)
#include "domain_tests.moc"
