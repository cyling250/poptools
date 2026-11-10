#pragma once

#include "../IFeatureModule.h"

#include <QObject>

class QAction;
class QTimer;

namespace poptools {

class ActivityStore;
class ForegroundTracker;
class GlobalKeyboardHook;
class GlobalMouseHook;
class ImeCommitDetector;
class InputActivityMonitor;
class KeyboardTextComposer;
class SnapshotOcrPipeline;

// ============================================================
// 活动监控功能模块：键盘键入记录 + 键鼠活跃时间线 + OCR 快照。
//
// 数据流（全部主线程事件循环，磁盘 IO 在 ActivityStore worker）：
//   GlobalKeyboardHook::keyEvent ─┬→ KeyboardTextComposer → keys.jsonl
//                                 └→ InputActivityMonitor ┐
//   GlobalMouseHook::mousePressed → InputActivityMonitor ├→ timeline.jsonl
//   ForegroundTracker::foregroundChanged → 两者换窗上下文 ┘
//
// 开关：Settings monitor/enabled（默认关闭）；托盘项一键启停；
// 设置页保存后经 settingsChanged 重应用。红线2豁免：开启期间
// WH_KEYBOARD_LL 常驻，前提是回调只入队（见各类头注释）。
// ============================================================
class ActivityMonitorModule : public QObject, public IFeatureModule
{
    Q_OBJECT
public:
    explicit ActivityMonitorModule(QObject *parent = nullptr);
    QString id() const override { return QStringLiteral("activitymonitor"); }
    void init(AppServices &services) override;
    void shutdown() override;

private slots:
    void applyRunningState(); // 按设置开停整套监控
    void toggleFromTray();

private:
    void startMonitor();
    void stopMonitor();

    AppServices *m_services = nullptr;
    QAction *m_trayAction = nullptr;

    // 运行期对象（startMonitor 创建 / stopMonitor 销毁）
    ActivityStore *m_store = nullptr;
    GlobalKeyboardHook *m_kbHook = nullptr;
    KeyboardTextComposer *m_composer = nullptr;
    InputActivityMonitor *m_activity = nullptr;
    ForegroundTracker *m_tracker = nullptr;
    GlobalMouseHook *m_mouseHook = nullptr; // 仅取"点击"信号喂状态机
    ImeCommitDetector *m_imeDetector = nullptr;   // OCR 快照链（可选）
    SnapshotOcrPipeline *m_ocrPipeline = nullptr; // 上屏 → 截图 → OCR → 增量
    QTimer *m_tick = nullptr;
    int m_appliedRetentionDays = -1;     // 最近一次启动注入 store 的保留天数（变化时重启）
    int m_appliedIdleThresholdSec = -1;  // 最近一次启动注入 monitor 的空闲阈值（变化时重启）
    bool m_appliedOcrSnapshot = false;   // 最近一次启动装配 OCR 链的开关（变化时重启）
};

} // namespace poptools
