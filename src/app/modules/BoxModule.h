#pragma once

#include <QObject>

#include "app/IFeatureModule.h"

#include <QPointer>
#include <QString>
#include <QVector>

class QWidget;

namespace poptools {

class AppServices;
class BoxCatalog;
class BoxPanel;

// ============================================================
// 桌面收纳盒功能模块（T-060 文件夹门户式、多盒）：每盒一个面板实例注册
// 到桌面表面宿主；仓库 BoxCatalog 持有全部盒定义（绑定文件夹 + 几何）。
// 结构性变更 → 重建面板实例；几何变更 → 面板原地重读。T-069b 托盘菜单
// 精简：管理/显隐托盘入口撤除，等价入口收进设置页通用页
//（boxOpenManager/boxAnyVisible/boxSetVisible 槽）。
// ============================================================
class BoxModule : public QObject, public IFeatureModule
{
    Q_OBJECT
public:
    explicit BoxModule(QObject *parent = nullptr);
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
    void reloadCatalog(); // box.dirty（MCP agent 改写）→ 重读仓库 + 面板重建
    void openManager(const QString &focusBoxId);
    BoxPanel *panelFor(const QString &boxId) const;

    BoxCatalog *m_catalog = nullptr;
    QVector<PanelEntry> m_panels;
    QPointer<QWidget> m_manager;
    AppServices *m_services = nullptr;
};

} // namespace poptools
