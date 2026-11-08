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

    // 设置页保存后的外观/行为重应用
    connect(&services, &AppServices::settingsChanged, this,
            &CalendarModule::onSettingsChanged);

    // 迷你条的数据刷新：数据变化 + 每分钟（跨天自动换内容）
    connect(m_store, &CalendarStore::changed, this, &CalendarModule::refreshMini);
    auto *minuteTimer = new QTimer(this);
    minuteTimer->setInterval(60 * 1000);
    connect(minuteTimer, &QTimer::timeout, this, &CalendarModule::refreshMini);
    minuteTimer->start();

    // 托盘菜单（排在既有功能模块之后）
    services.addTrayAction(QStringLiteral("桌面日历"), [this] { toggleWindow(); });

    // 开机自启后自动显示（组合根托盘 show 后由事件循环触发，晚于窗口系统就绪）
    if (Settings::instance().calendarShowOnStartup())
        QTimer::singleShot(0, QCoreApplication::instance(),
                           [this] { ensureWindow()->show(); });
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

void CalendarModule::toggleWindow()
{
    CalendarWindow *window = ensureWindow();
    if (window->isVisible()) {
        window->hide();
        return;
    }
    if (m_mini && m_mini->isVisible())
        m_mini->hide(); // 从迷你条唤起完整窗口时收起迷你条
    window->show();
    window->raise();
    window->activateWindow();
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
    m_mini->show();
    m_mini->raise();
}

void CalendarModule::showFullFromMini()
{
    if (m_mini)
        m_mini->hide();
    CalendarWindow *window = ensureWindow();
    window->show();
    window->raise();
    window->activateWindow();
}

void CalendarModule::onSettingsChanged()
{
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
