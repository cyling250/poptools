#pragma once

#include <QDate>
#include <functional>
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
    void render(bool force = false) override;
    void onMove(const QPoint &p) override;
    void onLeave() override;
    void onClick(const QPoint &p) override;
    void onDoubleClick(const QPoint &p) override;
    void onDutyStarted() override;
    void onDutyStopped() override;

    // ---- 模块 / 设置页入口 ----
    void showCalendar(); // 取消隐藏 + 渲染
    // 进入调整模式：弹 DesktopAdjustOverlay，期间 render()/事件被 m_adjusting 抑制，
    // 确认/取消/销毁三回调语义见 .cpp
    void enterAdjustMode();
    // 设置页保存日历设置后调用：重读 settings.json 并重绘
    void reloadSettings();
    // 托盘入口：内容搜索弹窗（trackModal），accept 后跳目标月并按 G-06 校验决定是否开编辑器
    void searchTasks();
    // 托盘入口：选日期弹窗（trackModal），accept 后仅翻到所选月（不开编辑器）
    void pickDate();
    // 六槽位实现：原生文件对话框 + JSON 导入导出（对话框不经 trackModal，
    // 模态抑制靠 eventsAccepted 的 activeModalWidget 兜底，T-062 G-10）
    void exportTasks();
    void importTasks();

    // ---- Goody 工作摘要（T-067；CalendarModule 注入 GoodyModule 查询槽） ----
    // 注入摘要查询槽（GoodyModule 发布；注入即强制刷新一次缓存）
    void setDigestQuery(std::function<QString(int year, int month, int day)> daily,
                        std::function<QString(int year, int month)> monthly);
    // 重建显示月的摘要缓存并重绘。force=false 时同月直接跳过（render 热路径
    // 去重）；翻月/goodySummariesChanged 信号走 force=true
    void refreshSummaries(bool force);

private:
    // 调整预览帧渲染（T-063 移植）：仅面板尺寸变化才重渲染（节流），移动模式
    // overlay 平移既有帧——桌面擦除链路退出拖动感知路径（跟手天花板）
    void updateAdjustPreview(bool force);
    // 渲染公共体（S-04）：hover/hint 为真实悬停值时画悬停反馈；suppressIfAdjusting
    // 时调整模式直接跳过（覆盖层自绘装饰）。commitDirty 空 = 整帧提交（默认）；
    // 悬停级小改动带脏矩形提交（对齐 BoxPanel 先例，T-071）
    void renderFrame(const QPoint &hover, const QString &hoverHint, bool suppressIfAdjusting,
                     const QRect &commitDirty = QRect());
    // 悬停语义目标 → 可视矩形（脏矩形计算用）：头部按钮/引导胶囊/月报链接，
    // 未命中的项不计入（P-063 口径：按会改变画面的语义目标计）
    QRect hoverVisualRect(int headerIdx, const QString &hint, bool monthHov) const;
    // 秒级跨日检测：仅日期变化才重绘；正处显示月则跟随跳到新月
    void tickClock();
    // 把设置矩形钳回主屏内（宽高下限 520/420），screen 无效时跳过
    void clampSettings();
    QPoint logicalPos(const QPoint &physical) const; // 物理 → Qt 逻辑
    // 线程池壁纸成品回主线程（30s 定时器异步路径尾段）：装缓存 + 重绘（T-068）
    void onWallpaperComputed(const QImage &img);
    // 壁纸重算异步踢发（duty 启动即踢一次 + 30s 定时器 + 设置重载共用）：
    // refreshNeeded 探测（签名未变免算）→ 线程池 computeBlurred → 回主线程
    // adoptWallpaper（T-071：壁纸重算全链离主线程，渲染取图只回缓存）
    void kickWallpaperRefresh();
    void openEditor(const QDate &date, const QRect &cell);
    // 模态信息框（trackModal 托管，show 后再居中）
    void showInfo(const QString &title, const QString &text);
    // 悬停按语义目标重绘（P-063）：仅头部按钮下标/胶囊文案变化才 render
    void updateHover(const QPoint &p);
    // 单击分发：头部翻页/调整 → 月报链接（T-067）→ 胶囊消失 → 格子（仅消引导）
    void handleSingleClick(const QPoint &p);
    // 双击显示月格子 → 打开当日日程编辑器
    void handleDoubleClick(const QPoint &p);
    void trackModal(QWidget *w);
    void centerOnPanel(QWidget *w);
    // explorer 转发事件是否放行。m_modal 管 trackModal 弹窗；原生文件对话框
    // 自带模态循环不经 trackModal，用 activeModalWidget 兜底（T-062 G-10 修复）
    bool eventsAccepted() const;

    QImage m_staging; // 面板私有整帧（渲染完成后经 surface 合成发布）
    std::unique_ptr<CalendarRenderer> m_renderer;
    CalendarSettings m_settings;
    CalendarTaskStore m_store;
    QDate m_today;
    int m_year = 0;
    int m_month = 0;
    std::optional<CalendarLayout> m_layout;
    QPoint m_hover = QPoint(-9999, -9999); // 无悬停哨兵，与 onLeave 清理值一致（S-11）
    QString m_hoverHint;
    int m_lastHeaderHover = -1;
    bool m_modal = false;
    bool m_adjusting = false;
    QRect m_screenNative;  // 日历归属屏物理矩形（虚拟桌面坐标；中心落在哪屏归哪屏）
    bool m_hiddenDuringAdjust = false; // 调整期间桌面层日历隐藏（槽置空，预览在覆盖层）
    QImage m_adjustPreview;  // 调整预览帧（面板 + 四周 32px 阴影/发光余量）
    qint64 m_previewMs = 0;  // 预览渲染节流：上次渲染时刻
    bool m_previewPending = false; // 节流补帧定时器在飞
    QPointer<QWidget> m_modalWidget;
    QTimer *m_clock = nullptr;
    QTimer *m_wallpaperTimer = nullptr;
    bool m_wallpaperBusy = false; // 壁纸线程池重算在飞（30s 定时器重入护栏，T-068）
    GlobalMouseHook *m_editorHook = nullptr; // 日程编辑器外点关闭兜底（随编辑器装卸）
    std::function<QString(int, int, int)> m_digestQuery; // Goody 查询槽转发（可空）
    std::function<QString(int, int)> m_monthlyQuery;
    CalendarDigestView m_digestView; // 显示月的摘要缓存（渲染唯一数据源）
    QString m_digestViewMonth;       // 缓存对应月份键（热路径去重）
    bool m_lastMonthLinkHov = false; // 月报链接悬停态（变化才重绘）
};

} // namespace poptools
