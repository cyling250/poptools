#include "SelectionTranslateModule.h"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QTimer>

#include "../AppServices.h"
#include "core/hotkey/GlobalMouseHook.h"
#include "core/settings/Settings.h"
#include "core/translation/SelectionTranslator.h"
#include "core/translation/Translator.h"
#include "ui/translate/TranslateCard.h"

#ifdef Q_OS_WIN
#include <windows.h> // 鼠标钩子回调中的 WindowFromPoint / GetWindowThreadProcessId
#endif

namespace poptools {

// 仅作 QObject 接线中枢；运行期对象在 init 建、shutdown 撤（钩子/过滤器）
SelectionTranslateModule::SelectionTranslateModule(QObject *parent)
    : QObject(parent)
{
}

void SelectionTranslateModule::init(AppServices &services)
{
    m_services = &services;

    // 抓取选中文本
    m_selTranslator = new SelectionTranslator(this);
    connect(m_selTranslator, &SelectionTranslator::textAcquired,
            this, &SelectionTranslateModule::onTextAcquired);
    connect(m_selTranslator, &SelectionTranslator::errorOccurred,
            this, &SelectionTranslateModule::onAcquireError);

    // 卡片翻译服务（唯一消费者是划词卡片）
    m_translator = new Translator(this);
    connect(m_translator, &Translator::translateFinished,
            this, &SelectionTranslateModule::onTranslateFinished);

    // 全局鼠标钩子：划词卡片不抢焦点，点击"其他应用"的窗口时 Qt 收不到任何事件，
    // 需系统级钩子感知（本应用内的点击仍由本类的应用级事件过滤器处理）
    m_mouseHook = new GlobalMouseHook(this);
    connect(m_mouseHook, &GlobalMouseHook::mousePressed, this,
            [this](const QPoint &nativePos) {
        if (!m_card || !m_card->isVisible())
            return;
#ifdef Q_OS_WIN
        // 落点窗口属于其他进程 → 用户点击了卡片外部（本应用外的窗口），隐藏卡片。
        // nativePos 为物理像素，直接与 Win32 窗口归属比较，不涉及 Qt 逻辑坐标换算
        const POINT pt{nativePos.x(), nativePos.y()};
        if (HWND hwnd = WindowFromPoint(pt)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != GetCurrentProcessId())
                hideCard();
        }
#else
        Q_UNUSED(nativePos);
#endif
    });

    // Ctrl+Alt+Shift+X 划词翻译：截图进行中不触发；热键在原生事件过滤器中触发，
    // UI 操作延迟到事件循环
    services.registerHotKey(
        Settings::instance().selectionTranslateHotKeyVk(),
        Settings::instance().selectionTranslateHotKeyModifiers(), [this] {
            QTimer::singleShot(0, QCoreApplication::instance(),
                               [this] { triggerTranslate(); });
        });

    // 应用级事件过滤器：卡片显示时点击本应用内卡片外部 → 隐藏卡片
    qApp->installEventFilter(this);
}

void SelectionTranslateModule::shutdown()
{
    hideCard();
    qApp->removeEventFilter(this); // init 安装的应用级过滤器必须反装配（对齐 shutdown 契约）
}

void SelectionTranslateModule::triggerTranslate()
{
    // 截图中不触发划词翻译
    if (m_services->captureActive && m_services->captureActive())
        return;
    m_selTranslator->trigger();
}

void SelectionTranslateModule::onTextAcquired(const QString &text)
{
    // 翻译器正忙（上一次翻译尚未返回）时放弃本次划词翻译
    if (m_translator->busy())
        return;

    // 全局唯一卡片：显示中则先隐藏（hideEvent 同步清空旧内容），
    // 再载入新原文显示在鼠标位置
    TranslateCard *card = ensureCard();
    card->hide();
    card->setSourceText(text); // 填原文 + 内容自适应 + 定位到鼠标附近
    card->show();
    m_mouseHook->start();      // 显示期间感知点击其他应用窗口（卡片外部）
    card->translateNow();      // 立即执行翻译（经 translateRequested 登记在途状态）
}

// 抓词失败也走卡片展示（复用唯一卡片定位纪律），让用户看见失败原因而非静默
void SelectionTranslateModule::onAcquireError(const QString &error)
{
    // 全局唯一卡片：显示获取选中文本失败的错误信息
    TranslateCard *card = ensureCard();
    card->hide();
    card->showError(error);
    card->moveNearCursor();
    card->show();
    m_mouseHook->start();
}

TranslateCard *SelectionTranslateModule::ensureCard()
{
    if (!m_card) {
        // 全局唯一无父窗口：进程退出由 OS 回收（析构不执行）；卡片无关键
        // 落盘/清理，hideEvent 即统一清空状态，故无析构依赖
        m_card = new TranslateCard();
        // 翻译/解释/总结/改写请求：用户点击卡片内动作按钮或新划词触发
        connect(m_card, &TranslateCard::translateRequested, this,
                [this](const QString &text, const QString &fromLang, const QString &toLang,
                       int action) {
                    m_translator->translate(text, fromLang, toLang, false,
                                            static_cast<LlmAction>(action));
                });
        // 用户主动关闭（关闭按钮 / Esc）：走统一隐藏路径清理状态
        connect(m_card, &TranslateCard::closed, this, [this] { hideCard(); });
    }
    return m_card;
}

void SelectionTranslateModule::hideCard()
{
    if (m_card)
        m_card->hide(); // 内容清空在 hideEvent 中统一处理
    if (m_mouseHook)
        m_mouseHook->stop(); // 卡片隐藏后不再需要感知全局点击
}

bool SelectionTranslateModule::eventFilter(QObject *watched, QEvent *event)
{
    // 划词卡片显示时点击卡片外部 → 隐藏卡片（内容清空由 hideEvent 统一处理）。
    // 卡片内部的顶层弹层（如下拉语言列表的容器窗口）不算"外部"。
    if (event->type() == QEvent::MouseButtonPress && m_card && m_card->isVisible()) {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && !m_card->isAncestorOf(widget)) {
            const QPoint globalPos =
                static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
            if (!m_card->frameGeometry().contains(globalPos))
                hideCard();
        }
    }
    return QObject::eventFilter(watched, event);
}

void SelectionTranslateModule::onTranslateFinished(bool ok, const QString &result,
                                                   const QString &error)
{
    // 本服务的唯一消费者是划词卡片：卡片显示中回填结果；
    // 卡片已被用户隐藏/关闭则静默丢弃在途结果（内容清空已由 hideEvent 完成）
    if (!m_card || !m_card->isVisible())
        return;
    if (ok)
        m_card->setTranslateResult(result);
    else
        m_card->showError(error);
}

} // namespace poptools
