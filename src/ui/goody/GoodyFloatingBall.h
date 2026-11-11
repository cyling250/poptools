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
// 脏矩形重绘；非工作态零动画零定时器，隐藏时动画一并停表。
// ============================================================
class GoodyFloatingBall : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal glowPhase READ glowPhase WRITE setGlowPhase)

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
    void startBreath();
    void stopBreath();
    QRect ballRect() const;      // 球圆形（窗口居中 52px）
    QRect glowDirtyRect() const; // 呼吸重绘脏矩形（球+光晕外沿）

    goodyball::GestureClassifier m_gesture;
    QPoint m_winPressOffset; // 按下点相对窗口左上角偏移（拖动跟手）
    bool m_hover = false;
    bool m_working = false;
    qreal m_glowPhase = 0.0;
    QPropertyAnimation *m_breath = nullptr; // 工作态呼吸光晕（懒建）
};

} // namespace poptools
