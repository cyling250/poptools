#pragma once

#include <QDate>
#include <QObject>
#include <QPoint>
#include <QWidget>

#include <memory>
#include <optional>
#include <vector>

#include "core/calendar/CalendarLayout.h"
#include "core/calendar/CalendarSettings.h"
#include "core/calendar/CalendarTaskStore.h"
#include "CalendarRenderer.h"
#include "CalendarSharedCanvas.h"

class QTimer;
class QLibrary;

namespace poptools {

// ============================================================
// 桌面日历宿主控制器（照搬 rlqd CalendarClone/HostForm.cs）：
// 隐藏 1x1 窗口承载 DLL 事件（WM_APP+0x103），QLibrary 加载注入 DLL 并
// StartDock；时钟（跨日重绘）/ 壁纸 30s 刷新 / 2s 看门狗（explorer 重启自愈）
// 三定时器；悬停/点击/双击命中经 CalendarLayout 分发到翻页、菜单、编辑器。
// 窗口本身位于屏幕外不激活；所有交互发生在 explorer 绘制的共享位图上。
// 坐标约定：布局/事件/画布一律物理像素，仅 Qt 原生弹窗定位处除以 dpr。
// ============================================================
class CalendarHostWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CalendarHostWidget(QWidget *parent = nullptr);
    ~CalendarHostWidget() override;

    // 装配：建画布/渲染/注入/启定时器（重活由模块经单发定时器延迟触发）
    void startDuty();
    // 停用：解除注入、停定时器（watchdog 不再自愈）
    void stopDuty();
    bool isStopped() const { return m_stopped; }

    // 托盘入口
    void showCalendar();        // 取消隐藏 + 渲染
    void enterAdjustMode();
    void openSettings();

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qint64 *result) override;

private:
    void render();
    void renderDuringAdjust();
    void tickClock();
    void startDock();
    void clampSettings();
    QPoint logicalPos(const QPoint &physical) const; // 物理 → Qt 逻辑
    void openEditor(const QDate &date, const QRect &cell);
    void openMainMenu();
    void openPluginMenu();
    void searchTasks();
    void pickDate();
    void exportTasks();
    void importTasks();
    void showInfo(const QString &title, const QString &text);
    void onDllEvent(int ev, int extra, const QPoint &p);
    void updateHover(const QPoint &p);
    void handleSingleClick(const QPoint &p);
    void handleDoubleClick(const QPoint &p);
    void notifyHookUpdate();

    CalendarSharedCanvas m_canvas;
    std::unique_ptr<CalendarRenderer> m_renderer;
    CalendarSettings m_settings;
    CalendarTaskStore m_store;
    QRect m_screen; // 主屏物理像素
    qreal m_dpr = 1.0;
    QDate m_today;
    int m_year = 0;
    int m_month = 0;
    std::optional<CalendarLayout> m_layout;
    QPoint m_hover;
    QString m_hoverHint;
    int m_lastHeaderHover = -1;
    std::optional<QPoint> m_mouseDown;
    bool m_modal = false;
    bool m_adjusting = false;
    bool m_stopped = false;
    bool m_settingsAccepted = false;
    QTimer *m_clock = nullptr;
    QTimer *m_wallpaperTimer = nullptr;
    QTimer *m_watchdog = nullptr;
    QLibrary *m_hookLib = nullptr;
    int (*m_startDock)(HWND) = nullptr;
    int (*m_stopDock)() = nullptr;
};

} // namespace poptools
