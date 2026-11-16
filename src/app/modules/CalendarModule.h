#pragma once

#include "app/IFeatureModule.h"

class QAction;

namespace poptools {

class AppServices;
class CalendarHostWidget;

// ============================================================
// 桌面日历功能模块：注入式贴壁纸日历（照搬 E:\rlqd 复刻实现）。
// init 只装配（宿主创建 + 延迟启动重活），托盘贡献显示/调整/设置/停用；
// 停用即解除 explorer 注入；退出前 shutdown 必须回调 StopDock。
// ============================================================
class CalendarModule : public IFeatureModule
{
public:
    QString id() const override { return QStringLiteral("calendar"); }
    void init(AppServices &services) override;
    void shutdown() override;

private:
    CalendarHostWidget *m_host = nullptr;
    QAction *m_toggleAction = nullptr;
    AppServices *m_services = nullptr; // shutdown 撤销能力槽用
};

} // namespace poptools
