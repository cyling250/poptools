#include "BoxModule.h"

#include "app/AppServices.h"
#include "ui/box/BoxManagerDialog.h"
#include "ui/box/BoxPanel.h"

#include <QDir>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QObject>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace poptools {

// 装配：读仓库 → 逐盒建面板注册宿主 → 1.5s 延迟拉值守（重活后置，与日历同纪律）；
// 贡献托盘「收纳盒管理」「显示·隐藏收纳盒」
BoxModule::BoxModule(QObject *parent)
    : QObject(parent)
{
}

void BoxModule::init(AppServices &services)
{
    m_services = &services;
    m_catalog = new BoxCatalog(BoxCatalog::load());

    if (services.desktopSurface) {
        rebuildPanels();
        // 表面宿主延迟拉起（重活：画布/注入/壁纸模糊；与日历模块同纪律）
        QTimer::singleShot(1500, services.desktopSurface, [this] {
            if (m_services && m_services->desktopSurface)
                m_services->desktopSurface->startDuty();
        });
    }

    // T-069b 托盘菜单精简：「收纳盒管理」「隐藏收纳盒」托盘入口撤除，等价
    // 入口收进设置页通用页「收纳盒」卡片组（管理对话框 + 显示开关）。
    // 显隐语义沿用原 toggleShow：任一可见 → 全隐藏，否则全显示（零盒时
    // 不动，BUG-08 口径保持）
    // T-074 M2.5：MCP 服务器（dsh agent）改写 boxes.json 后 touch box.dirty →
    // 实时重载（收纳盒目录/新增/解绑立即上屏；AI 整理桌面免重启）
    {
        const QString dataRoot =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        auto *watcher = new QFileSystemWatcher(this);
        if (QDir(dataRoot).exists())
            watcher->addPath(dataRoot);
        connect(watcher, &QFileSystemWatcher::directoryChanged, this,
                [this, dataRoot](const QString &) {
                    if (!QFileInfo::exists(dataRoot + QStringLiteral("/box.dirty")))
                        return;
                    QFile::remove(dataRoot + QStringLiteral("/box.dirty"));
                    reloadCatalog();
                });
    }

    services.boxOpenManager = [this] { openManager(QString()); };
    services.boxAnyVisible = [this] {
        if (!m_catalog)
            return false;
        return std::any_of(m_catalog->boxes().cbegin(), m_catalog->boxes().cend(),
                           [](const BoxDef &d) { return !d.hidden; });
    };
    services.boxSetVisible = [this](bool visible) {
        if (!m_catalog || m_catalog->boxes().isEmpty())
            return;
        m_catalog->setHiddenAll(!visible); // visible=true → 全显示（清 hidden）
        for (const PanelEntry &e : m_panels) {
            if (e.panel)
                e.panel->reloadDef();
        }
    };
}

// 反注册全部面板并销毁实例（宿主槽位/命中随之失效）；能力槽同步撤销防悬垂
void BoxModule::shutdown()
{
    if (m_services) {
        m_services->boxOpenManager = nullptr;  // T-069b
        m_services->boxAnyVisible = nullptr;
        m_services->boxSetVisible = nullptr;
    }
    if (m_services && m_services->desktopSurface) {
        for (const PanelEntry &e : m_panels)
            m_services->desktopSurface->unregisterPanel(e.surfaceId);
    }
    for (const PanelEntry &e : m_panels) {
        if (e.panel)
            e.panel->deleteLater();
    }
    m_panels.clear();
    if (m_manager)
        m_manager->deleteLater();
    delete m_catalog;
    m_catalog = nullptr;
}

void BoxModule::rebuildPanels()
{
    if (!m_services || !m_services->desktopSurface || !m_catalog)
        return;
    DesktopSurfaceHost *surface = m_services->desktopSurface;
    // 旧面板按注册 id 注销（hitPanel/合成次序随之失效，防悬垂面板引用）
    for (const PanelEntry &e : m_panels) {
        surface->unregisterPanel(e.surfaceId);
        if (e.panel)
            e.panel->deleteLater();
    }
    m_panels.clear();
    for (const BoxDef &d : m_catalog->boxes()) {
        // 无 QObject 父：面板生命周期由 m_panels 登记 + shutdown 显式 deleteLater
        auto *panel = new BoxPanel(m_catalog, d.id);
        const int id = surface->registerPanel(panel);
        m_panels.append({ d.id, id, panel });
    }
}

// box.dirty（MCP agent 改写仓库）→ 重读仓库 + 全面板重建。
// 重建走 rebuildPanels 全量路径：注销旧面板→销毁→按新仓库重建（面板数与
// 绑定目录都可能变）；显隐态以新仓库为准
void BoxModule::reloadCatalog()
{
    if (!m_catalog)
        return;
    BoxCatalog fresh = BoxCatalog::load();
    *m_catalog = fresh;
    rebuildPanels();
}

void BoxModule::openManager(const QString &focusBoxId)
{
    if (!m_catalog)
        return;
    if (m_manager) {
        // 已开着：定位到请求的盒并前置（P-058 家族：防重复弹窗竞态）。
        // QDialog 关闭只 hide 不销毁——"已开着"分支必须先 show 补位，
        // 否则 raise/activateWindow 抬起的是一个隐藏窗，表现为"再点没反应"
        auto *dlg = qobject_cast<BoxManagerDialog *>(m_manager.data());
        if (dlg) {
            if (!focusBoxId.isEmpty())
                dlg->focusBox(focusBoxId);
            dlg->show();
            dlg->raise();
            dlg->activateWindow();
        }
        return;
    }
    auto *dlg = new BoxManagerDialog(*m_catalog, nullptr);
    m_manager = dlg;
    QObject::connect(dlg, &QObject::destroyed, dlg, [this, dlg] {
        if (m_manager == dlg)
            m_manager = nullptr;
    });
    // 结构变更（新建/解绑）→ 重建面板实例；调整请求 → 对应面板进调整模式
    QObject::connect(dlg, &BoxManagerDialog::catalogChanged, dlg, [this] { rebuildPanels(); });
    QObject::connect(dlg, &BoxManagerDialog::adjustRequested, dlg,
                     [this](const QString &boxId) {
                         if (BoxPanel *p = panelFor(boxId))
                             p->enterAdjustMode();
                     });
    if (!focusBoxId.isEmpty())
        dlg->focusBox(focusBoxId);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

// 线性查盒 id → 面板实例；nullptr = 无此盒（刚解绑未重建）
BoxPanel *BoxModule::panelFor(const QString &boxId) const
{
    for (const PanelEntry &e : m_panels) {
        if (e.panel && e.boxId == boxId)
            return e.panel;
    }
    return nullptr;
}

} // namespace poptools
