#include "CalendarModule.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

#include "../AppServices.h"
#include "core/calendar/CalendarStore.h"
#include "core/settings/Settings.h"
#include "ui/calendar/CalendarMiniWidget.h"
#include "ui/calendar/CalendarWindow.h"

namespace poptools {

CalendarModule::CalendarModule(QObject *parent)
    : QObject(parent)
{
}

void CalendarModule::init(AppServices &services)
{
    m_services = &services;
    m_store = services.calendarStore;
    Q_ASSERT(m_store && "组合根必须先创建共享 CalendarStore 再装配模块");

    // 设置页保存后：外观重应用 + 按「显示桌面日历」开关显隐
    connect(&services, &AppServices::settingsChanged, this, &CalendarModule::onSettingsChanged);

    // 迷你条的数据刷新：数据变化 + 每分钟（跨天自动换内容）
    connect(m_store, &CalendarStore::changed, this, &CalendarModule::refreshMini);
    auto *minuteTimer = new QTimer(this);
    minuteTimer->setInterval(60 * 1000);
    connect(minuteTimer, &QTimer::timeout, this, &CalendarModule::refreshMini);
    minuteTimer->start();

    // 设置开启时随应用启动显示（窗口在事件循环就绪后创建，桌面 Explorer 已就绪）
    if (Settings::instance().calendarEnabled())
        QTimer::singleShot(0, QCoreApplication::instance(),
                           [this] { applyVisibility(); });
}

void CalendarModule::shutdown()
{
    // 数据随每次修改即时落盘，无需在此补写；隐藏窗口即可
    if (m_window)
        m_window->hide();
    if (m_mini)
        m_mini->hide();
}

CalendarWindow *CalendarModule::ensureWindow()
{
    if (!m_window) {
        m_window = new CalendarWindow(m_store); // 全局唯一实例，随 QApplication 退出清理
        connect(m_window, &CalendarWindow::miniRequested, this, &CalendarModule::showMini);
    }
    return m_window;
}

void CalendarModule::applyVisibility()
{
    if (Settings::instance().calendarEnabled()) {
        CalendarWindow *window = ensureWindow();
        if (m_mini && m_mini->isVisible())
            m_mini->hide(); // 从迷你条唤起完整窗口时收起迷你条
        window->show();
        // 不 raise()：贴底窗口必须待在一切普通窗口之下，
        // 显示后由贴底守护定时器归位
    } else {
        if (m_window)
            m_window->hide();
        if (m_mini)
            m_mini->hide();
    }
}

void CalendarModule::showMini()
{
    if (!m_mini) {
        m_mini = new CalendarMiniWidget(m_store);
        connect(m_mini, &CalendarMiniWidget::expandRequested, this,
                &CalendarModule::showFullFromMini);
    }
    m_mini->applySettings();
    m_mini->refresh();
    // 主屏右上角（可用区避开任务栏），贴边留 10px
    const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
    if (!m_mini->isVisible())
        m_mini->move(avail.right() - m_mini->width() - 10, avail.top() + 10);
    m_mini->show(); // 不 raise()：贴底窗口不得抬升
}

void CalendarModule::showFullFromMini()
{
    if (m_mini)
        m_mini->hide();
    applyVisibility(); // 与设置开关保持一致（开着才显示）
}

void CalendarModule::onSettingsChanged()
{
    applyVisibility();
    if (m_window)
        m_window->applySettings();
    if (m_mini)
        m_mini->applySettings();
    refreshMini();
}

void CalendarModule::refreshMini()
{
    if (m_mini && m_mini->isVisible())
        m_mini->refresh();
}

} // namespace poptools
