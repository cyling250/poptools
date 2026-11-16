#pragma once

#include "app/IFeatureModule.h"

#include <QPointer>
#include <QString>
#include <QVector>

class QAction;
class QWidget;

namespace poptools {

class AppServices;
class BoxCatalog;
class BoxPanel;

// ============================================================
// 桌面收纳盒功能模块（T-060 文件夹门户式、多盒）：每盒一个面板实例注册
// 到桌面表面宿主；仓库 BoxCatalog 持有全部盒定义（绑定文件夹 + 几何）。
// 托盘贡献：收纳盒管理 / 显示·隐藏收纳盒；结构性变更 → 重建面板实例；
// 几何变更 → 面板原地重读。
// ============================================================
class BoxModule : public IFeatureModule
{
public:
    QString id() const override { return QStringLiteral("box"); }
    void init(AppServices &services) override;
    void shutdown() override;

private:
    struct PanelEntry
    {
        QString boxId;
        int surfaceId = -1; // DesktopSurfaceHost::registerPanel 返回值
        BoxPanel *panel = nullptr;
    };

    void rebuildPanels();
    void openManager(const QString &focusBoxId);
    BoxPanel *panelFor(const QString &boxId) const;

    BoxCatalog *m_catalog = nullptr;
    QVector<PanelEntry> m_panels;
    QPointer<QWidget> m_manager;
    QAction *m_toggleAction = nullptr;
    AppServices *m_services = nullptr;
};

} // namespace poptools
