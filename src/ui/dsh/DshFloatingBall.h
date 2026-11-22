#pragma once

#include <QPoint>
#include <QWidget>

#include "DshBallFx.h"
#include "DshBallLogic.h"

class QTimer;

namespace poptools {

// ============================================================
// Goody 桌面悬浮球（T-050 阶段一）：Goody 的常驻唤起入口。
//
// 呈现：无边框透明工具窗，88px 窗体中央 52px 玻璃感圆球，paintEvent
// 委托 DshBallFx 分层渲染（T-076 全量动效批：极光球/能量核心/星尘
// 环绕/涟漪波纹/彗尾光弧/辉光脉冲/果冻拖拽/磁吸微移/呼吸波发射等
// 18 项，见 DshBallFx.h 分层说明），中央自绘 "G" 字标。
//
// 驻桌：desktopstack::attach(Ball 层)——贴 Progman 上方、1s 守护
// 归位；WM_MOUSEACTIVATE → MA_NOACTIVATE（P-031：点击不激活窗口）。
//
// 手势：按下-移动超 6px（dshball::GestureClassifier 状态机，可
// 单测）=拖动（窗口跟手，松开发 positionChanged）；位移未超阈值
// =单击（发 clicked）。位置持久化经模块写 dsh/ballPos。
//
// 动效时钟：可见期间 30fps 主时钟（QTimer 33ms）统一推进全部相位并
// 脏矩形重绘；隐藏停钟零开销。磁吸经 QCursor::pos() 低频轮询（窗不
// 悬停也能感知鼠标接近）。屏幕布局变化（拔屏/主屏切换）即时重钳制。
// ============================================================
class DshFloatingBall : public QWidget
{
    Q_OBJECT

public:
    explicit DshFloatingBall(QWidget *parent = nullptr);

    void setWorking(bool on);  // 工作态开关（模块接 workingChanged）→ 呼吸波/弹跳
    void applySavedPosition(); // 读 ini 记忆位（空=默认右上角）→ 钳制 → move

signals:
    void clicked();                          // 单击 → 模块转 DshModule::openChat
    void positionChanged(const QPoint &pos); // 拖动松开 → 模块持久化 ballPos

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override; // 贴底 attach + 启动效时钟
    void hideEvent(QHideEvent *event) override; // detach + 停时钟
    bool nativeEvent(const QByteArray &eventType, void *message,
                     qintptr *result) override; // MA_NOACTIVATE（P-031）

private slots:
    void applyTheme();
    void onFxTick(); // 30fps 主时钟：磁吸目标 + 相位推进 + 脏矩形重绘

private:
    void startFxClock();
    void stopFxClock();
    QRect ballRect() const;      // 球圆形（窗口居中 52px）
    QRect glowDirtyRect() const; // 动效重绘脏矩形（球+光效外沿余量）

    dshball::GestureClassifier m_gesture;
    QPoint m_winPressOffset; // 按下点相对窗口左上角偏移（拖动跟手）
    DshBallFxState m_fx;   // 全部动效状态（渲染唯一数据源）
    QTimer *m_fxClock = nullptr; // 30fps 主时钟（懒建，随可见启停）
};

} // namespace poptools
