#pragma once

#include <QDate>
#include <QObject>
#include <QPointer>
#include <QPoint>
#include <optional>

#include "core/calendar/CalendarLayout.h"
#include "core/calendar/CalendarSettings.h"
#include "core/calendar/CalendarTaskStore.h"
#include "ui/calendar/CalendarRenderer.h"
#include "ui/shared/DesktopSurfaceHost.h"

class QTimer;

namespace poptools {

class GlobalMouseHook;

// ============================================================
// 桌面日历面板（T-059 从 CalendarHostWidget 抽取日历业务，骨架移交
// DesktopSurfaceHost；交互/渲染语义照搬 rlqd CalendarClone/HostForm.cs）。
// 时钟（跨日重绘）/ 壁纸 30s 刷新两定时器归面板；注入/看门狗/事件路由/
// 整帧发布归表面宿主。悬停/点击/双击命中经 CalendarLayout 分发到翻页、
// 调整、编辑器。坐标约定：布局/事件/画布一律物理像素，仅 Qt 原生弹窗
// 定位处除以 dpr。
// ============================================================
class CalendarPanel : public QObject, public DesktopPanel
{
    Q_OBJECT
public:
    explicit CalendarPanel(QObject *parent = nullptr);
    ~CalendarPanel() override;

    // ---- DesktopPanel ----
    QRect rectPhysical() const override;
    QImage *frame() override;
    void render() override;
    void onMove(const QPoint &p) override;
    void onLeave() override;
    void onClick(const QPoint &p) override;
    void onDoubleClick(const QPoint &p) override;
    void onDutyStarted() override;
    void onDutyStopped() override;

    // ---- 模块 / 设置页入口 ----
    void showCalendar(); // 取消隐藏 + 渲染
    void enterAdjustMode();
    // 设置页保存日历设置后调用：重读 settings.json 并重绘
    void reloadSettings();
    void searchTasks();
    void pickDate();
    void exportTasks();
    void importTasks();

private:
    void renderDuringAdjust();
    void tickClock();
    void clampSettings();
    QPoint logicalPos(const QPoint &physical) const; // 物理 → Qt 逻辑
    void openEditor(const QDate &date, const QRect &cell);
    void showInfo(const QString &title, const QString &text);
    void updateHover(const QPoint &p);
    void handleSingleClick(const QPoint &p);
    void handleDoubleClick(const QPoint &p);
    void trackModal(QWidget *w);
    void centerOnPanel(QWidget *w);
    bool eventsAccepted() const { return !m_modal && !m_adjusting; }

    QImage m_staging; // 面板私有整帧（渲染完成后经 surface 合成发布）
    std::unique_ptr<CalendarRenderer> m_renderer;
    CalendarSettings m_settings;
    CalendarTaskStore m_store;
    QDate m_today;
    int m_year = 0;
    int m_month = 0;
    std::optional<CalendarLayout> m_layout;
    QPoint m_hover;
    QString m_hoverHint;
    int m_lastHeaderHover = -1;
    bool m_modal = false;
    bool m_adjusting = false;
    QPointer<QWidget> m_modalWidget;
    QTimer *m_clock = nullptr;
    QTimer *m_wallpaperTimer = nullptr;
    GlobalMouseHook *m_editorHook = nullptr; // 日程编辑器外点关闭兜底（随编辑器装卸）
};

} // namespace poptools
