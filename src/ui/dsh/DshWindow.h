#pragma once

#include <QLabel>
#include <QWidget>

#include "DshRuntime.h"
#include "WebView2Host.h"

namespace poptools {

// ============================================================
// DshWindow：dsh 常驻窗（T-074 M1 融合批——旧 GoodyWindow 的替代）。
//
// 形态：原生系统标题栏 + WebView2Host 填满客户区（内容即 dsh 原版界面，
// 无任何 Qt 包装层）。运行语义（用户 2026-11-21 拍板）：
//   * PopTools 存续期 sidecar 常驻后台——构造即启动 runtime，不随窗口
//     开关生死；定时任务/在途回合不依赖窗口可见；
//   * 关闭按钮/Esc = 隐藏回后台（hide），再次唤起秒开；
//   * 启动期/失败态给居中状态层（Ready 后隐藏，纯 dsh 内容呈现）。
// 几何记忆：Settings "dsh/windowGeometry"。
// ============================================================
class DshWindow : public QWidget
{
    Q_OBJECT
public:
    explicit DshWindow(QWidget *parent = nullptr);

    // 运行时装配（模块 init 调用一次）；随后立即启动 sidecar（常驻）。
    // WebView2 的原生初始化推迟到首次 openWindow（隐藏期预建原生窗是黑框根因）
    void configure(const QString &nodeExe, const QString &dshBinJs, const QString &dshHome,
                   const QString &agentsHome, const QString &apiKey,
                   const QString &userDataDir = QString());
    // 唤起入口（悬浮球/热键/设置页）：显示 + 前置 + 激活
    void openWindow();

signals:
    // sidecar 工作态广播：Starting=true，Ready/Failed=false（悬浮球呼吸光效）
    void workingChanged(bool working);
    // 窗口隐藏（回后台）——悬浮球按开关回归
    void windowHidden();

protected:
    void closeEvent(QCloseEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void setStatusVisible(bool visible);
    void rebuildStatus();

    DshRuntime m_runtime;
    WebView2Host *m_web = nullptr;
    QLabel *m_status = nullptr; // 启动中/失败覆盖层（Ready 后隐藏）
    QString m_lastFailure;      // 失败文案缓存（状态层重建用）
};

} // namespace poptools
