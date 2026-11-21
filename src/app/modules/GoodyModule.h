#pragma once

#include "../IFeatureModule.h"

#include <QObject>

namespace poptools {

class DshWindow;
class GoodyFloatingBall;

// ============================================================
// Goody 管家功能模块（T-074 dsh 原样移植后瘦身）：
//   * dsh 常驻窗（DshWindow，WebView2 嵌 dsh 原版界面）+ sidecar 常驻管理；
//   * 桌面悬浮球（唯一自动唤起入口）+ Ctrl+Alt+G 热键 + 设置页打开入口；
//   * 日历工作摘要查询槽（goody/daily、goody/monthly 文件约定路径，
//     M2 经 MCP 桥由 dsh 定时任务写入续命）。
//
// 旧对话/工具/权限/记忆/日报成文已随 dsh 原生能力退役（会话事件日志、
// 审批、compaction、schedule 均由 dsh host 承担）。
// ============================================================
class GoodyModule : public QObject, public IFeatureModule
{
    Q_OBJECT
public:
    explicit GoodyModule(QObject *parent = nullptr);
    QString id() const override { return QStringLiteral("goody"); }
    void init(AppServices &services) override;
    void shutdown() override;

signals:
    // sidecar 工作态广播（Starting 呼吸 / Ready、Failed 停）——悬浮球呼吸光效
    void workingChanged(bool working);

private:
    void ensureDshWindow();      // dsh 常驻窗懒建（init 即建即启动 sidecar）
    void ensureBall();           // 悬浮球懒建（开关开且未建时）
    void applyBallSettings();    // 球开关/位置重应用（init 与设置变更共用）
    void openChat();

    AppServices *m_services = nullptr;
    DshWindow *m_dshWindow = nullptr;    // dsh 常驻窗（sidecar 常驻 + WebView2 内容）
    // 桌面悬浮球：无父窗口，进程退出由 OS 回收（析构不执行）——球无关键
    // 落盘（位置在拖动时即时持久化），无析构依赖
    GoodyFloatingBall *m_ball = nullptr;
    bool m_working = false;              // 工作态缓存（去重广播用）
};

} // namespace poptools
