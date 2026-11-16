#pragma once

#include "app/IFeatureModule.h"

class QAction;

namespace poptools {

class AppServices;
class CalendarPanel;

// ============================================================
// 桌面日历功能模块：注入式贴壁纸日历（照搬 E:\rlqd 复刻实现）。
// 注入通道与共享画布由组合根的 DesktopSurfaceHost 承载（T-059 起，与收纳盒
// 共用）；本模块只装配日历面板（重活延迟启动）+ 托盘贡献显示/调整/设置/停用。
// ============================================================
class CalendarModule : public IFeatureModule
{
public:
    QString id() const override { return QStringLiteral("calendar"); }
    void init(AppServices &services) override;
    void shutdown() override;

private:
    CalendarPanel *m_panel = nullptr;
    int m_panelId = -1;
    QAction *m_toggleAction = nullptr;
    AppServices *m_services = nullptr; // shutdown 撤销能力槽用
};

} // namespace poptools
