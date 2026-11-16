#include "BoxModule.h"

#include "app/AppServices.h"
#include "ui/box/BoxManagerDialog.h"
#include "ui/box/BoxPanel.h"

#include <QAction>
#include <QObject>
#include <QTimer>

#include <algorithm>

namespace poptools {

// 装配：读仓库 → 逐盒建面板注册宿主 → 1.5s 延迟拉值守（重活后置，与日历同纪律）；
// 贡献托盘「收纳盒管理」「显示·隐藏收纳盒」
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

    const auto toggleShow = [this] {
        if (!m_catalog || !m_toggleAction)
            return;
        // T-062 修复: 零盒时翻转文案会变成谎话（桌面无盒可显隐，下一次点击又翻回，
        // 还白落盘一次），直接不动（BUG-08）
        if (m_catalog->boxes().isEmpty())
            return;
        const bool anyVisible =
            std::any_of(m_catalog->boxes().cbegin(), m_catalog->boxes().cend(),
                        [](const BoxDef &d) { return !d.hidden; });
        m_catalog->setHiddenAll(anyVisible); // 全可见 → 全隐藏；否则全显示
        for (const PanelEntry &e : m_panels) {
            if (e.panel)
                e.panel->reloadDef();
        }
        m_toggleAction->setText(anyVisible ? QStringLiteral("显示收纳盒")
                                           : QStringLiteral("隐藏收纳盒"));
    };

    services.addTrayAction(QStringLiteral("收纳盒管理"),
                           [this] { openManager(QString()); });
    // 盒头 ✛ 即调整入口（同日历头部），托盘不再重复
    m_toggleAction = services.addTrayAction(QStringLiteral("隐藏收纳盒"), toggleShow);
}

// 反注册全部面板并销毁实例（宿主槽位/命中随之失效）；托盘 action 归 AppServices，
// 不在此析构
void BoxModule::shutdown()
{
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
