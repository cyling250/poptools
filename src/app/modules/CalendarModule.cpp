#include "CalendarModule.h"

#include "app/AppServices.h"
#include "ui/calendar/CalendarPanel.h"

#include <QAction>
#include <QTimer>

namespace poptools {

void CalendarModule::init(AppServices &services)
{
    // 面板本身很轻（重活：设置/日程 JSON、渲染器资源在 onDutyStarted 懒建），
    // 注入与画布在表面宿主 startDuty——按性能红线 6，启动后延迟一拍再拉起
    m_services = &services;
    Q_ASSERT(services.desktopSurface);
    if (services.desktopSurface) {
        m_panel = new CalendarPanel();
        m_panelId = services.desktopSurface->registerPanel(m_panel);
        // 上下文用表面宿主：它若先亡则回调自动丢弃
        QTimer::singleShot(1200, services.desktopSurface, [this] {
            if (m_services && m_services->desktopSurface)
                m_services->desktopSurface->startDuty();
        });
    }

    // 能力槽发布：日历设置入口已并入应用设置对话框「桌面日历」页（组合根发布
    // openCalendarSettings），此处只暴露设置页需要回连面板的功能
    services.calendarApplySettings = [this] {
        if (m_panel)
            m_panel->reloadSettings();
    };
    services.calendarOpenSearch = [this] {
        if (m_panel)
            m_panel->searchTasks();
    };
    services.calendarPickDate = [this] {
        if (m_panel)
            m_panel->pickDate();
    };
    services.calendarExportTasks = [this] {
        if (m_panel)
            m_panel->exportTasks();
    };
    services.calendarImportTasks = [this] {
        if (m_panel)
            m_panel->importTasks();
    };

    const auto showCal = [this] {
        if (m_panel)
            m_panel->showCalendar();
    };
    const auto adjust = [this] {
        if (m_panel)
            m_panel->enterAdjustMode();
    };
    const auto openSettings = [&services] {
        if (services.openCalendarSettings)
            services.openCalendarSettings();
    };
    const auto toggleDuty = [this] {
        if (!m_services || !m_services->desktopSurface || !m_toggleAction)
            return;
        if (m_services->desktopSurface->isStopped()) {
            m_services->desktopSurface->resumeDuty();
            m_toggleAction->setText(QStringLiteral("停用桌面日历"));
        } else {
            m_services->desktopSurface->stopDuty();
            m_toggleAction->setText(QStringLiteral("启用桌面日历"));
        }
    };

    services.addTrayAction(QStringLiteral("显示桌面日历"), showCal);
    services.addTrayAction(QStringLiteral("调整日历位置和大小"), adjust);
    services.addTrayAction(QStringLiteral("日历设置"), openSettings);
    m_toggleAction = services.addTrayAction(QStringLiteral("停用桌面日历"), toggleDuty);
}

void CalendarModule::shutdown()
{
    // 撤销发布到 AppServices 的能力槽（析构面板内防悬垂回调）；
    // 注入解除（StopDock）由组合根析构 DesktopSurfaceHost 时统一执行
    if (m_services) {
        m_services->calendarApplySettings = nullptr;
        m_services->calendarOpenSearch = nullptr;
        m_services->calendarPickDate = nullptr;
        m_services->calendarExportTasks = nullptr;
        m_services->calendarImportTasks = nullptr;
    }
    if (m_services && m_services->desktopSurface && m_panel)
        m_services->desktopSurface->unregisterPanel(m_panelId);
    delete m_panel;
    m_panel = nullptr;
}

} // namespace poptools
