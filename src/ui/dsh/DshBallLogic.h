#pragma once

#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QtGlobal>

namespace poptools {
namespace dshball {

// ============================================================
// 悬浮球纯逻辑（T-050 阶段一）：与窗口/Qt 事件解耦，便于单测。
//
//   * GestureClassifier：点击 vs 拖动判别状态机——按下后累计位移
//     超过阈值即"拖动"（闩锁，之后回落原点仍算拖动）；松开时位移
//     一直未超阈值即"单击"。
//   * clampBallPos：把球左上角钳进可见屏幕（多屏取包含点的那块，
//     不在任何屏内取中心最近的一块），防止记忆位置因换分辨率/
//     拔显示器而落进不可达区域。
//   * ballPosToString / ballPosFromString：dsh/ballPos 的
//     "x,y" 字符序列化往返。
//   * defaultBallPos：默认位——可用区（排除任务栏）右上角内缩。
// ============================================================

// 拖动判别阈值（px，曼哈顿距离）：超过即判拖动，否则单击
constexpr int kBallDragThreshold = 6;
// 默认位内缩边距（px）
constexpr int kBallDefaultMargin = 12;

enum class Gesture
{
    None, // 无按下（如中键释放）
    Click,
    Drag
};

class GestureClassifier
{
public:
    // 按下（注入全局坐标）
    void beginPress(const QPoint &globalPos)
    {
        m_phase = Phase::Pressed;
        m_press = globalPos;
        m_last = globalPos;
    }
    // 移动注入：位移超阈值即闩锁进拖动态（之后回落原点不改判）
    void addMove(const QPoint &globalPos)
    {
        if (m_phase == Phase::Idle)
            return;
        m_last = globalPos;
        if (m_phase == Phase::Pressed
            && (globalPos - m_press).manhattanLength() > kBallDragThreshold)
            m_phase = Phase::Dragging;
    }
    // 松开：判别并复位。按住未动 → Click；拖动中 → Drag
    Gesture endRelease()
    {
        const Phase p = m_phase;
        m_phase = Phase::Idle;
        if (p == Phase::Pressed)
            return Gesture::Click;
        if (p == Phase::Dragging)
            return Gesture::Drag;
        return Gesture::None;
    }
    bool dragging() const { return m_phase == Phase::Dragging; }
    // 按下点 → 当前点的累计位移（拖动跟手：窗口 move 用）
    QPoint totalDelta() const { return m_last - m_press; }

private:
    enum class Phase
    {
        Idle,
        Pressed,
        Dragging
    };
    Phase m_phase = Phase::Idle;
    QPoint m_press;
    QPoint m_last;
};

// 位置钳制：pos 为球左上角，保证整球落在 screens 之一内。
// 屏幕表为空（异常环境）原样返回；多屏取包含球中心的那块，
// 全都不含则取中心距最近的一块（返回后球在该屏内可见）。
inline QPoint clampBallPos(const QPoint &pos, const QSize &size,
                           const QList<QRect> &screens)
{
    if (screens.isEmpty() || size.isEmpty())
        return pos;
    const QPoint center = pos + QPoint(size.width() / 2, size.height() / 2);
    const QRect *target = nullptr;
    for (const QRect &r : screens) {
        if (r.contains(center)) {
            target = &r;
            break;
        }
    }
    if (!target) {
        qint64 best = qint64(Q_INT64_C(1)) << 62;
        for (const QRect &r : screens) {
            const QPoint d = r.center() - center;
            const qint64 dist = qint64(d.x()) * d.x() + qint64(d.y()) * d.y();
            if (dist < best) {
                best = dist;
                target = &r;
            }
        }
    }
    return QPoint(qBound(target->left(), pos.x(), target->right() + 1 - size.width()),
                  qBound(target->top(), pos.y(), target->bottom() + 1 - size.height()));
}

// 默认位：可用区（排除任务栏）右上角内缩，不遮任务栏/屏幕边缘
inline QPoint defaultBallPos(const QRect &avail, const QSize &size)
{
    return QPoint(avail.right() + 1 - size.width() - kBallDefaultMargin,
                  avail.top() + kBallDefaultMargin);
}

// "x,y" 序列化（负坐标屏幕支持：坐标可为负）
inline QString ballPosToString(const QPoint &pos)
{
    return QStringLiteral("%1,%2").arg(pos.x()).arg(pos.y());
}

// 解析失败（空/格式坏/非数字）返回 QPoint()（isNull），调用方回落默认位
inline QPoint ballPosFromString(const QString &xy, bool *ok = nullptr)
{
    if (ok)
        *ok = false;
    const QStringList parts = xy.split(QLatin1Char(','));
    if (parts.size() != 2)
        return QPoint();
    bool xOk = false, yOk = false;
    const int x = parts.at(0).trimmed().toInt(&xOk);
    const int y = parts.at(1).trimmed().toInt(&yOk);
    if (!xOk || !yOk)
        return QPoint();
    if (ok)
        *ok = true;
    return QPoint(x, y);
}

} // namespace dshball
} // namespace poptools
