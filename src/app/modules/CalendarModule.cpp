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
    m_host = new CalendarHostWidget;
    QTimer::singleShot(1200, m_host, [this] {
        if (m_host)
            m_host->startDuty();
    });

    const auto showCal = [this] {
        if (m_host)
            m_host->showCalendar();
    };
    const auto adjust = [this] {
        if (m_host)
            m_host->enterAdjustMode();
    };
    const auto openSettings = [this] {
        if (m_host)
            m_host->openSettings();
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
    // 退出前必须解除 explorer 子类化（StopDock），否则桌面残留日历位图
    if (m_host) {
        m_host->stopDuty();
        m_host->deleteLater();
        m_host = nullptr;
    }
}

} // namespace poptools
