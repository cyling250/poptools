#include "CalendarModule.h"

#include "app/AppServices.h"
#include "ui/calendar/CalendarHostWidget.h"

#include <QAction>
#include <QTimer>

namespace poptools {

void CalendarModule::init(AppServices &services)
{
    // 宿主窗口很轻（1x1 屏幕外），画布/注入/壁纸模糊是重活——按性能红线 6，
    // init 只装配，启动后延迟一拍再拉起
    m_services = &services;
    m_host = new CalendarHostWidget;
    QTimer::singleShot(1200, m_host, [this] {
        if (m_host)
            m_host->startDuty();
    });

    // 能力槽发布：日历设置入口已并入应用设置对话框「桌面日历」页（组合根发布
    // openCalendarSettings），此处只暴露设置页需要回连宿主的功能
    services.calendarApplySettings = [this] {
        if (m_host)
            m_host->reloadSettings();
    };
    services.calendarOpenSearch = [this] {
        if (m_host)
            m_host->searchTasks();
    };
    services.calendarPickDate = [this] {
        if (m_host)
            m_host->pickDate();
    };
    services.calendarExportTasks = [this] {
        if (m_host)
            m_host->exportTasks();
    };
    services.calendarImportTasks = [this] {
        if (m_host)
            m_host->importTasks();
    };

    const auto showCal = [this] {
        if (m_host)
            m_host->showCalendar();
    };
    const auto adjust = [this] {
        if (m_host)
            m_host->enterAdjustMode();
    };
    const auto openSettings = [&services] {
        if (services.openCalendarSettings)
            services.openCalendarSettings();
    };
    const auto toggleDuty = [this] {
        if (!m_host || !m_toggleAction)
            return;
        if (m_host->isStopped()) {
            m_host->showCalendar();
            m_toggleAction->setText(QStringLiteral("停用桌面日历"));
        } else {
            m_host->stopDuty();
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
    // 退出前必须解除 explorer 子类化（StopDock），否则桌面残留日历位图；
    // 同时撤销发布到 AppServices 的能力槽（析构窗口内防悬垂回调）
    if (m_services) {
        m_services->calendarApplySettings = nullptr;
        m_services->calendarOpenSearch = nullptr;
        m_services->calendarPickDate = nullptr;
        m_services->calendarExportTasks = nullptr;
        m_services->calendarImportTasks = nullptr;
    }
    if (m_host) {
        m_host->stopDuty();
        m_host->deleteLater();
        m_host = nullptr;
    }
}

} // namespace poptools
