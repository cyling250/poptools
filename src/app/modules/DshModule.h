#pragma once

#include "../IFeatureModule.h"

#include <QObject>

namespace poptools {

class DshFloatingBall;

// ============================================================
// DeepSeek Harness 后台服务模块（T-078 M1 桌面端形态）：
//   * 主界面 = dsh 官方 Electron 桌面端（独立应用，自带 host/更新/账号），
//     本模块只负责「唤起」：悬浮球/热键/设置页 → 启动或前置桌面端进程；
//   * 桌面集成装配（init 一次性）：MCP patch 幂等写入桌面端 profile、
//     AGENTS.md 人格基线迁移、旧 goody 数据目录迁移；
//   * 桌面悬浮球 + Ctrl+Alt+G 热键 + 设置页打开入口；
//   * MCP 心跳 → 悬浮球呼吸（dsh agent 调 PopTools 工具时点亮）；
//   * 日历工作摘要查询槽（dsh/daily、dsh/monthly 文件约定路径，
//     M2 起 dsh 定时任务经 MCP 桥写入续命）。
//
// T-078 M1 退役：DshWindow/WebView2Host/DshRuntime（sidecar+看门狗）/
// DshUpdateService——被官方桌面端整体替代。
// ============================================================
class DshModule : public QObject, public IFeatureModule
{
    Q_OBJECT
public:
    explicit DshModule(QObject *parent = nullptr);
    QString id() const override { return QStringLiteral("dsh"); }
    void init(AppServices &services) override;
    void shutdown() override;

signals:
    // 工作态广播（MCP 心跳点亮，10s 无心跳回静默）——悬浮球呼吸光效
    void workingChanged(bool working);

private:
    void ensureDesktopIntegration(); // 桌面端集成装配（init 一次：patch/迁移）
    void ensureHeartbeatWatcher();   // MCP 心跳监听（悬浮球呼吸数据源）
    void ensureBall();               // 悬浮球懒建（开关开且未建时）
    void applyBallSettings();        // 球开关/位置重应用（init 与设置变更共用）
    void openChat();                 // 唯一唤起点：启动/前置桌面端进程

    AppServices *m_services = nullptr;
    // 桌面悬浮球：无父窗口，进程退出由 OS 回收（析构不执行）——球无关键
    // 落盘（位置在拖动时即时持久化），无析构依赖
    DshFloatingBall *m_ball = nullptr;
    bool m_working = false;              // 工作态缓存（去重广播用）
};

} // namespace poptools
