#pragma once

#include "../IFeatureModule.h"

#include <QObject>

namespace poptools {

class AppServices;
class BoxStore;
class BoxWindow;

// ============================================================
// 桌面收纳盒功能模块：贴在桌面层的分类卡片（手机文件夹式收纳）。
//
// 显示/隐藏由设置页「显示桌面收纳盒」开关驱动；托盘菜单提供
// 「新建收纳盒…」入口（模块关闭时点击会自动启用）。
// 每个盒子一个 BoxWindow（贴桌、拖拽收纳、目录即真实文件夹），
// 数据存 BoxStore（JSON）。
// ============================================================
class BoxModule : public QObject, public IFeatureModule
{
    Q_OBJECT
public:
    explicit BoxModule(QObject *parent = nullptr);
    QString id() const override { return QStringLiteral("box"); }
    void init(AppServices &services) override;
    void shutdown() override;

private slots:
    void applyVisibility();   // 按设置开关 + 仓库内容同步全部盒子窗口
    void onSettingsChanged();
    void createBox();         // 托盘「新建收纳盒…」
    void refreshBoxes();      // 仓库变化 → 增删窗口

private:
    AppServices *m_services = nullptr;
    BoxStore *m_store = nullptr;
    QList<BoxWindow *> m_windows;
    BoxWindow *ensureWindow(const QString &boxId);
};

} // namespace poptools
