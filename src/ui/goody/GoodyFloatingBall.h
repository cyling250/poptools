#pragma once

#include <QPoint>
#include <QWidget>

#include "GoodyBallLogic.h"

class QPropertyAnimation;

namespace poptools {

// ============================================================
// Goody 桌面悬浮球（T-050 阶段一）：Goody 的常驻唤起入口。
//
// 呈现：无边框透明工具窗，68px 窗体中央 52px 玻璃感圆球，paintEvent
// 全自绘（无子控件无 QSS），中央自绘 "G" 字标（resources 无 goody
// 专属图标，AI 代决：自绘字母 + 复用 app_icon.svg 作窗口图标）；
// 颜色取 ThemeColors 随系统明暗切换（colorSchemeChanged）。
//
// 驻桌：desktopstack::attach(Ball 层)——贴 Progman 上方、1s 守护
// 归位；不走 T-048 壁纸带（球需要真实点击）。WM_MOUSEACTIVATE →
// MA_NOACTIVATE（P-031：点击不激活窗口、不抬 Z 序，但鼠标消息
// 照常到达，单击/拖动手势可用）。
//
// 手势：按下-移动超 6px（goodyball::GestureClassifier 状态机，可
// 单测）=拖动（窗口跟手，松开发 positionChanged）；位移未超阈值
// =单击（发 clicked）。位置持久化经模块写 assistant/ballPos。
//
// 呼吸光效：setWorking(true)（LLM 请求在途或工具执行期）时外圈
// accent 光晕 1.2s 周期呼吸（QPropertyAnimation 驱动自绘描边），
// 脏矩形重绘；隐藏时动画一并停表。
//
// 动效三件套（T-076 用户拍板 2026-11-22）：
//   待机 —— 非 working 期间一圈 accent 锥形渐变光环绕球缓转（8s/圈，
//           QTimer 90ms 步进 10fps，alpha 峰值 55，克制不抢视线）；
//   悬停 —— 进入时一道白色斜向高光扫过球面（450ms 单次 OutCubic，
//           clip 球圆）+ 既有描边 accent 亮起；
//   呼吸 —— working 强化：内环 alpha 呼吸 + 外环半径随相位微缩放，
//           双层立体感。
// 屏幕布局变化（拔屏/主屏切换/改分辨率）即时重钳制位置——旧记忆坐标
// 落在已拔屏幕上会缺角/失踪（2026-11-22 用户实测）。
// ============================================================
class GoodyFloatingBall : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal glowPhase READ glowPhase WRITE setGlowPhase)
    Q_PROPERTY(qreal sweepPhase READ sweepPhase WRITE setSweepPhase)

public:
    explicit GoodyFloatingBall(QWidget *parent = nullptr);

    void setWorking(bool on);  // 工作呼吸光效开关（模块接 workingChanged）
    void applySavedPosition(); // 读 ini 记忆位（空=默认右上角）→ 钳制 → move

signals:
    void clicked();                          // 单击 → 模块转 GoodyModule::openChat
    void positionChanged(const QPoint &pos); // 拖动松开 → 模块持久化 ballPos

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override; // 贴底 attach
    void hideEvent(QHideEvent *event) override; // detach + 停呼吸
    bool nativeEvent(const QByteArray &eventType, void *message,
                     qintptr *result) override; // MA_NOACTIVATE（P-031）

private slots:
    void applyTheme();

private:
    qreal glowPhase() const { return m_glowPhase; }
    void setGlowPhase(qreal phase); // 动画驱动：只脏球+光晕矩形
    qreal sweepPhase() const { return m_sweepPhase; }
    void setSweepPhase(qreal phase);
    void startBreath();
    void stopBreath();
    void setIdleRunning(bool on); // 待机光环步进定时器启停（可见且非 working）
    QRect ballRect() const;      // 球圆形（窗口居中 52px）
    QRect glowDirtyRect() const; // 光效重绘脏矩形（球+光晕外沿）

    goodyball::GestureClassifier m_gesture;
    QPoint m_winPressOffset; // 按下点相对窗口左上角偏移（拖动跟手）
    bool m_hover = false;
    bool m_working = false;
    qreal m_glowPhase = 0.0;
    qreal m_sweepPhase = 0.0;
    qreal m_idlePhase = 0.0;              // 待机光环相位（0→1 绕球一圈）
    QTimer *m_idleTimer = nullptr;        // 待机环步进（懒建；90ms=10fps 足够）
    QPropertyAnimation *m_breath = nullptr; // 工作态呼吸光晕（懒建）
    QPropertyAnimation *m_sweep = nullptr;  // 悬停扫光（懒建，单次）
};

} // namespace poptools
