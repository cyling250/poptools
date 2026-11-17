#pragma once

#include <QImage>
#include <QRect>
#include <QVector>
#include <QWidget>

#include "DesktopSharedCanvas.h"

class QLibrary;
class QTimer;

namespace poptools {

// ============================================================
// 桌面面板接口：桌面表面上的一个自绘区域（T-059 起日历/收纳盒共用注入
// 通道）。生命周期：组合根创建 DesktopSurfaceHost → 模块 init 构造面板
// 并 registerPanel；模块 shutdown 时 unregisterPanel。
// 坐标一律主屏物理像素（画布坐标），弹窗定位由面板自行换算（÷dpr）。
// 槽位动态分配：commit 时按注册序写入共享头槽矩形（后注册=画在上层=
// 命中优先，T-060）。
// ============================================================
class DesktopPanel
{
public:
    virtual ~DesktopPanel() = default;

    virtual QRect rectPhysical() const = 0; // 命中与脏矩形（空矩形 = 不参与命中/失效）
    virtual QImage *frame() = 0;            // 最近整帧（画布尺寸；尚未渲染过返回 nullptr）
    virtual void render(bool force = false) = 0; // 重绘进 frame()，末尾调 commitSurface（force=确认立即恢复）

    // 鼠标事件（explorer 转发，物理像素）。move/leave 广播给全部面板（跨面板
    // 清 hover）；click/dblclk 只发给命中面板（surface 负责按下/抬起配对与
    // 图标穿透）。模态抑制由面板自行处理。
    virtual void onMove(const QPoint &) {}
    virtual void onLeave() {}
    virtual void onDown(const QPoint &) {}
    virtual void onClick(const QPoint &) {}
    virtual void onDoubleClick(const QPoint &) {}

    // 值守启停通知：启 = 注入生效可渲染上屏；停 = 解除注入，停一切自主动作
    virtual void onDutyStarted() {}
    virtual void onDutyStopped() {}

    void attachSurface(class DesktopSurfaceHost *surface) { m_surface = surface; }
    DesktopSurfaceHost *surface() const { return m_surface; }

protected:
    DesktopSurfaceHost *m_surface = nullptr; // registerPanel 时由宿主回填
};

// ============================================================
// 桌面表面宿主（T-059 从 CalendarHostWidget 抽取日历无关骨架）：
// 隐藏 1x1 窗口承载 DLL 转发的鼠标事件 → 按面板矩形路由；持有共享画布、
// 注入 DLL 生命周期（QLibrary + StartDock/StopDock + 2s 看门狗自愈），
// commitPanel 把各面板整帧合成后原子发布进共享位图（T-058 撕帧纪律）。
// 窗口位于屏幕外不激活；所有交互发生在 explorer 绘制的共享位图上。
// ============================================================
class DesktopSurfaceHost : public QWidget
{
    Q_OBJECT
public:
    explicit DesktopSurfaceHost(QWidget *parent = nullptr);
    ~DesktopSurfaceHost() override;

    int registerPanel(DesktopPanel *panel); // 返回面板 id（<0 失败）；重复注册忽略
    // 按 id 反注册：同步清点击配对与面板回引；后续面板槽位前移，DLL 侧旧矩形
    // 失效逻辑兜底清残影
    void unregisterPanel(int id);

    DesktopSharedCanvas &canvas() { return m_canvas; }
    QRect screenPhysical() const { return m_screen; }
    qreal dpr() const { return m_dpr; }
    bool isStopped() const { return m_stopped; }

    // 面板渲染完成后调用：合成全部面板帧 → 原子发布 → 通知 explorer 重绘。
    // dirtyPhysical 非空时只合成/发布该矩形（悬停这类高频小改动免整屏合成
    // 与整屏 memcpy）；空 = 整帧
    void commitSurface(const QRect &dirtyPhysical = QRect());

    // 值守：建画布/注入/看门狗并通知面板（幂等，可多模块重复调度）；
    // stopDuty 解除注入并通知面板暂停；resumeDuty 供「显示」类入口复活
    void startDuty();
    void stopDuty();
    void resumeDuty();

    // 弹窗锚定指定面板中心（物理 → Qt 逻辑换算；show 后尺寸才定的窗体由
    // 调用方 show 后再调一次，见 P-059）
    void centerOnPanelRect(QWidget *w, const QRect &panelPhysical) const;

    // 全桌面失效兜底（DLL POPTOOLS_MSG_CLONE_UPDATE wParam!=0）：窄失效区域
    // 遗漏时（确认恢复等场景）一次性全桌面重绘补齐
    void requestFullRepaint();

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qint64 *result) override;

private:
    bool ensureHookLib(); // 惰性加载注入 DLL 并解析 StartDock/StopDock 导出
    void startDock();
    void notifyHookUpdate();
    static HWND findHookFrameworkWindow();
    DesktopPanel *hitPanel(const QPoint &physical) const;

    struct PanelSlot
    {
        int id = 0;
        DesktopPanel *panel = nullptr; // 所有权在模块（shutdown 时 unregisterPanel）
    };

    DesktopSharedCanvas m_canvas;
    QImage m_composite; // 合成帧（各面板帧 → 此处 → 整帧发布）
    QVector<PanelSlot> m_panels;
    int m_nextPanelId = 1;
    QRect m_screen; // 主屏物理像素
    qreal m_dpr = 1.0;
    QTimer *m_watchdog = nullptr;
    QLibrary *m_hookLib = nullptr;
    int (*m_startDock)(HWND) = nullptr;
    int (*m_stopDock)() = nullptr;
    bool m_stopped = false;
    bool m_dutyStarted = false;
    DesktopPanel *m_downPanel = nullptr; // 点击配对（按下所在面板）
    QPoint m_downPoint;
};

} // namespace poptools
