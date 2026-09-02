#pragma once

#include <QtTest/QtTest>

#include <memory>
#include <vector>

#include "app/AppServices.h"
#include "app/FeatureRegistry.h"
#include "core/annotation/AnnotationCanvas.h"
#include "core/annotation/AnnotationItem.h"
#include "core/translation/Languages.h"
#include "core/translation/Translator.h"

using namespace snipaste;

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
};

QTEST_MAIN(DomainTests)
#include "domain_tests.moc"
