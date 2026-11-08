#pragma once

#include "../IFeatureModule.h"

#include <QObject>

namespace poptools {

class AppServices;
class CalendarMiniWidget;
class CalendarStore;
class CalendarWindow;

// ============================================================
// 桌面日历功能模块：月历 + 日程 + 待办的桌面常驻小组件。
//
// 数据只存本地（CalendarStore JSON），托盘菜单「桌面日历」开关窗口；
// 窗口支持置顶 / 贴边自动收起 / 透明度 / 收起到右上角迷你待办条
// （CalendarMiniWidget）。设置页保存后经 AppServices::settingsChanged
// 全量重应用外观。
// ============================================================
class CalendarModule : public QObject, public IFeatureModule
{
    Q_OBJECT
public:
    explicit CalendarModule(QObject *parent = nullptr);
    QString id() const override { return QStringLiteral("calendar"); }
    void init(AppServices &services) override;
    void shutdown() override;

private slots:
    void toggleWindow();
    void showMini();
    void showFullFromMini();
    void onSettingsChanged();
    void refreshMini();

private:
    CalendarWindow *ensureWindow(); // 懒创建（模块 init 只装配，重 UI 首用再建）

    AppServices *m_services = nullptr;
    CalendarStore *m_store = nullptr;
    CalendarWindow *m_window = nullptr;
    CalendarMiniWidget *m_mini = nullptr;
};

} // namespace poptools
