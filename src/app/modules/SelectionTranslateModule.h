#pragma once

#include <QObject>

#include "../IFeatureModule.h"

namespace poptools {

class AppServices;
class GlobalMouseHook;
class SelectionTranslator;
class TranslateCard;
class Translator;

// ============================================================
// 划词翻译模块：全局热键抓取选中文本 → 大模型翻译 → 唯一浮动卡片。
//
// 自包含该功能的全部系统级接触面：
//   * Ctrl+Alt+Shift+X 热键（经 AppServices 注册）；
//   * SelectionTranslator（模拟 Ctrl+C 抓取选中文本）；
//   * Translator（卡片翻译服务，唯一消费者是卡片）；
//   * TranslateCard 全局单例（复用，hideEvent 统一清空）；
//   * 全局鼠标钩子（点击其他应用的窗口 → 隐藏卡片）；
//   * 应用级事件过滤器（点击本应用内卡片外部 → 隐藏卡片）。
// ============================================================
class SelectionTranslateModule : public QObject, public IFeatureModule
{
    Q_OBJECT
public:
    explicit SelectionTranslateModule(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("selectionTranslate"); }
    void init(AppServices &services) override;
    void shutdown() override;

protected:
    // 应用级事件过滤器：划词卡片显示时点击本应用内卡片外部 → 隐藏卡片。
    // 卡片内部的顶层弹层（如下拉语言列表的容器窗口）不算"外部"
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    TranslateCard *ensureCard(); // 取全局唯一划词卡片（首次调用时创建）
    void hideCard();             // 隐藏卡片并停止鼠标钩子（内容清空由 hideEvent 完成）
    void triggerTranslate();     // 热键入口：截图态不触发，否则抓取选中文本
    void onTextAcquired(const QString &text);
    void onAcquireError(const QString &error);
    void onTranslateFinished(bool ok, const QString &result, const QString &error);

    AppServices *m_services = nullptr;
    Translator *m_translator = nullptr;             // 卡片翻译服务
    SelectionTranslator *m_selTranslator = nullptr; // 抓取选中文本
    GlobalMouseHook *m_mouseHook = nullptr;         // 感知点击其他应用窗口（卡片外部）
    TranslateCard *m_card = nullptr;                // 全局唯一划词卡片（复用）
};

} // namespace poptools
