#pragma once

#include "../IFeatureModule.h"

#include <QDate>
#include <QObject>

namespace poptools {

class AppServices;
class CalendarMiniWidget;
class CalendarStore;
class CalendarWindow;

// ============================================================
// 桌面日历功能模块：贴在桌面层的月历+日程小组件。
//
// 显示/隐藏完全由设置页「显示桌面日历」开关驱动（无托盘菜单项）：
// 勾选即显示并随应用启动自动出现，取消即隐藏。数据只存本地
// （CalendarStore JSON）。窗口默认固定模式（钉在桌面 WorkerW 层，
// 被普通窗口遮挡），标题栏 ✏️ 进入编辑模式可移动/缩放。
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
    void showMini();
    void showFullFromMini();
    void onSettingsChanged();
    void refreshMini();
    void onMinuteTick(); // 分钟 tick：迷你条刷新 + 主窗口跨午夜刷新（R5-B5）
    void applyVisibility(); // 按设置开关显示/隐藏

private:
    CalendarWindow *ensureWindow(); // 懒创建（模块 init 只装配，重 UI 首用再建）

    AppServices *m_services = nullptr;
    CalendarStore *m_store = nullptr;
    CalendarWindow *m_window = nullptr;
    CalendarMiniWidget *m_mini = nullptr;
    QDate m_lastDay; // 分钟 tick 的跨天判定基准（init 时置为当天）
};

} // namespace poptools
