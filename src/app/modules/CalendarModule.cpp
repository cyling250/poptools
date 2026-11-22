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
        // T-062 修复: G-09 上下文原为 desktopSurface（无 parent、活到进程末尾），
        // lambda 捕获模块裸 this——启动后 1.2s 内退出时，事件循环可能在模块析构后
        // 仍处理到期定时器 → 读已析构对象（UB）。上下文改面板：shutdown 先于宿主
        // 析构 delete m_panel，面板亡则回调自动丢弃；面板存活到宿主亡，届时
        // registerPanel 期建立的 surface 回引随宿主同灭，lambda 不再执行
        QTimer::singleShot(1200, m_panel, [panel = m_panel] {
            if (panel && panel->surface())
                panel->surface()->startDuty();
        });
        // T-067：工作摘要查询槽注入（组合根装配序保证 DshModule 先于日历
        // init，槽位已发布）+ 数据变更信号 → 刷新缓存重绘（摘要静默生成后
        // 日历链接自动出现/更新）
        m_panel->setDigestQuery(services.dshDailyDigestPath,
                                services.dshMonthlyDigestPath);
        QObject::connect(&services, &AppServices::dshSummariesChanged, m_panel,
                         [panel = m_panel] {
                             if (panel)
                                 panel->refreshSummaries(true);
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

    const auto adjust = [this] {
        if (m_panel)
            m_panel->enterAdjustMode();
    };

    // T-069b 托盘菜单精简：四个日历托盘入口全部撤除，等价入口收进设置页
    //「桌面日历」页——「调整位置和大小」按钮（calendarAdjust）、
    //「启用桌面日历」开关（calendarDutyStopped/calendarSetDuty，桌面表面
    // duty 语义，与原「停用桌面日历」一致）；「显示桌面日历」与设置页既有
    // 「显示日历清单」开关语义重叠、「日历设置」即设置页自身，直接删
    services.calendarAdjust = adjust;
    services.calendarDutyStopped = [this] {
        return m_services && m_services->desktopSurface
                   ? m_services->desktopSurface->isStopped()
                   : false;
    };
    services.calendarSetDuty = [this](bool enable) {
        if (!m_services || !m_services->desktopSurface)
            return;
        if (enable)
            m_services->desktopSurface->resumeDuty();
        else
            m_services->desktopSurface->stopDuty();
    };
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
        m_services->calendarAdjust = nullptr;        // T-069b
        m_services->calendarDutyStopped = nullptr;
        m_services->calendarSetDuty = nullptr;
    }
    if (m_services && m_services->desktopSurface && m_panel)
        m_services->desktopSurface->unregisterPanel(m_panelId);
    delete m_panel;
    m_panel = nullptr;
}

} // namespace poptools
