// T-076 全量动效批（2026-11-22 用户拍板"全要"）：悬浮球 18 项动效系统。
// 分层渲染与状态推进全在本文件（无 QObject）；接线见 DshFloatingBall。
#include "DshBallFx.h"

#include <QConicalGradient>
#include <QEasingCurve>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>

namespace poptools {

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr double kR = 26.0;          // 球半径基准（52px 直径）
constexpr double kBreathDur = 1.2;   // 呼吸周期（与窗体侧常量一致）

// 指数逼近（帧率无关平滑；rate≈10 即 ~0.3s 收敛）
void easeTo(double &value, double target, double dt, double rate)
{
    value += (target - value) * std::min(1.0, dt * rate);
}

// 阻尼弹簧（用于需要"过冲回弹"的果冻/弹跳）
void spring(double &value, double &vel, double target, double dt,
            double stiffness, double damping)
{
    const double a = (target - value) * stiffness - vel * damping;
    vel += a * dt;
    value += vel * dt;
}

// 极光三色（T-076 主题：青/紫/橙，与 Fluent 深色玻璃底兼容）
QColor auroraColor(int i, int alpha)
{
    static const QColor kColors[] = {
        QColor(79, 209, 197),   // 青
        QColor(159, 122, 234),  // 紫
        QColor(246, 173, 85),   // 橙（暖强调同族）
    };
    QColor c = kColors[i % 3];
    c.setAlpha(alpha);
    return c;
}

} // namespace

// ---- 事件触发 ----

void fxSeedDust(DshBallFxState &fx)
{
    fx.dust.clear();
    QRandomGenerator *rng = QRandomGenerator::global();
    for (int i = 0; i < 10; ++i) {
        DshBallFxState::Dust d;
        d.orbit = 1.04 + rng->generateDouble() * 0.30;  // 1.04r~1.34r
        d.speed = (0.25 + rng->generateDouble() * 0.55)  // rad/s 慢速飘行
                  * (rng->generateDouble() < 0.4 ? -1.0 : 1.0);
        d.phase = rng->generateDouble() * kTwoPi;
        d.size = 1.0 + rng->generateDouble() * 1.4;     // 1~2.4px
        d.twinkle = 1.5 + rng->generateDouble() * 3.0;  // 闪烁频率
        fx.dust.append(d);
    }
}

void fxSpawnRipple(DshBallFxState &fx, const QPointF &p)
{
    // #11 点击涟漪：点击点为源 3 圈错相扩散（球面水波）
    for (int i = 0; i < 3; ++i) {
        DshBallFxState::Wave w;
        w.center = p;
        w.spawnT = fx.t + i * 0.15;               // 错相
        w.maxR = kR * 1.35;
        w.dur = 0.6;
        w.alpha0 = 110 - i * 28;
        w.width = 2.0 - i * 0.5;
        w.clipBall = true;
        fx.waves.append(w);
    }
}

void fxKickJelly(DshBallFxState &fx, double strength)
{
    // #4 果冻：松手冲击 → scaleY 阻尼振荡（strength≈速度比例）
    fx.jellyVel += strength;
}

void fxKickBounce(DshBallFxState &fx)
{
    // #18 完成弹跳：scaleY 下压回弹 + 一圈快速爆闪扩散
    fx.bounceVel -= 7.0;
    DshBallFxState::Wave w;
    w.center = fx.ball.center();
    w.spawnT = fx.t;
    w.maxR = kR + 13;
    w.dur = 0.45;
    w.alpha0 = 150;
    w.width = 3.5;
    w.clipBall = false;
    fx.waves.append(w);
}

// ---- 主时钟推进 ----

void tickBallFx(DshBallFxState &fx, double dt)
{
    fx.t += dt;

    // 相位推进（#17：working 期间极光流速 ×3 = 思考流光）
    const double auroraSpeed = fx.working ? 0.75 : 0.25; // 圈/s
    fx.auroraPhase = std::fmod(fx.auroraPhase + auroraSpeed * dt, 1.0);
    fx.haloPhase = std::fmod(fx.haloPhase + dt / 8.0, 1.0);        // 彗尾 8s/圈
    fx.ringPhase = std::fmod(fx.ringPhase + dt / 5.0, 1.0);        // 反转环 5s/圈

    // working 呼吸相位 + 每拍发射扩散波（#16）
    fx.lastBreathPhase = fx.breathPhase;
    if (fx.working)
        fx.breathPhase = std::fmod(fx.breathPhase + dt / kBreathDur, 1.0);
    if (fx.working && fx.breathPhase < fx.lastBreathPhase) {       // 相位回绕=新一拍
        DshBallFxState::Wave w;
        w.center = fx.ball.center();
        w.spawnT = fx.t;
        w.maxR = kR + 12;
        w.dur = 0.9;
        w.alpha0 = 90;
        w.width = 2.5;
        w.clipBall = false;
        fx.waves.append(w);
    }

    // #8 辉光脉冲：4~8s 随机闪一次，0.25s 衰减
    fx.pulseCountdown -= dt;
    if (fx.pulseCountdown <= 0.0) {
        fx.pulseWave = 1.0;
        fx.pulseCountdown = 4.0 + QRandomGenerator::global()->generateDouble() * 4.0;
    }
    fx.pulseWave = std::max(0.0, fx.pulseWave - dt / 0.25);

    // 弹簧/逼近（#13 放大 / #4 按压 / #14 磁吸 / #10 星尘轨道）
    easeTo(fx.hoverScale, fx.hover ? 1.08 : 1.0, dt, 12.0);
    easeTo(fx.pressScale, fx.pressed ? 0.92 : (fx.dragging ? 0.95 : 1.0), dt, 14.0);
    easeTo(fx.magnetX, fx.magnetTargetX, dt, 10.0);
    easeTo(fx.magnetY, fx.magnetTargetY, dt, 10.0);
    easeTo(fx.dustRadius, fx.working ? 1.32 : (fx.hover ? 0.72 : 1.0), dt, 6.0);
    spring(fx.jelly, fx.jellyVel, 0.0, dt, 90.0, 9.0);   // 果冻：欠阻尼回弹
    spring(fx.bounce, fx.bounceVel, 0.0, dt, 110.0, 10.0); // 弹跳：轻过冲

    // 波生命周期清理（错相未出生的 spawnT>t 保留）
    fx.waves.erase(std::remove_if(fx.waves.begin(), fx.waves.end(),
                                  [&](const DshBallFxState::Wave &w) {
                                      return fx.t - w.spawnT > w.dur;
                                  }),
                   fx.waves.end());

    // #15 拖动彗尾：拖动中按帧采样球心（限 5 个），松手逐帧淡出
    if (fx.dragging) {
        fx.trailTimer += dt;
        if (fx.trailTimer >= 0.03) {
            fx.trailTimer = 0.0;
            fx.trail.append(fx.ball.center());
            if (fx.trail.size() > 5)
                fx.trail.removeFirst();
        }
    } else if (!fx.trail.isEmpty()) {
        fx.trail.removeFirst();
    }
}

// ---- 分层渲染 ----

void paintBallFx(QPainter *p, const DshBallFxState &fx)
{
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF ball = fx.ball;
    const QRectF realBall = ball.translated(fx.magnetX, fx.magnetY); // 含磁吸视觉位
    const QPointF c = realBall.center();
    const double r = realBall.width() / 2.0;

    // ---- L1 拖动彗尾（#15）：球心历史采样渐淡残影 ----
    for (int i = 0; i < fx.trail.size(); ++i) {
        const double k = double(i + 1) / fx.trail.size(); // 越新越实
        QColor ghost{fx.cardBg};
        ghost.setAlpha(int(10 + 22 * k));
        p->setPen(Qt::NoPen);
        p->setBrush(ghost);
        p->drawEllipse(fx.trail.at(i), r * (0.55 + 0.35 * k), r * (0.55 + 0.35 * k));
    }

    // ---- L2 光环系统 ----
    if (fx.working) {
        // 呼吸双层（内环 alpha+半径、外环扩散；半径上限 ≤ r+10 保窗内）
        const double wave = 0.5 - 0.5 * std::cos(kTwoPi * fx.breathPhase);
        QColor glow{fx.accent};
        glow.setAlpha(int(45 + 75 * wave));
        p->setPen(QPen(glow, 3));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(c, r + 2 + 1.5 * wave, r + 2 + 1.5 * wave);
        QColor outer{fx.accent};
        outer.setAlpha(int(18 + 40 * wave));
        p->setPen(QPen(outer, 2));
        p->drawEllipse(c, r + 4 + 2 * wave, r + 4 + 2 * wave);
    } else {
        // 待机彗尾弧（#6）：24 段圆弧，头亮尾长绕球缓转
        const double head = -fx.haloPhase * 360.0;
        for (int i = 0; i < 24; ++i) {
            const double a0 = head + i * 3.0;
            const double fade = 1.0 - double(i) / 24.0;
            QColor seg{fx.accent};
            seg.setAlpha(int(80 * fade * fade));
            p->setPen(QPen(seg, 1.5 + 1.5 * fade));
            p->setBrush(Qt::NoBrush);
            p->drawArc(realBall.adjusted(-5, -5, 5, 5), int(a0 * 16), int(3.2 * 16));
        }
        // 双层反转环（#7）：细环反向旋转（外层反向偏置）
        QColor ring{fx.accent};
        ring.setAlpha(34);
        p->setPen(QPen(ring, 1));
        p->setBrush(Qt::NoBrush);
        p->drawArc(realBall.adjusted(-7.5, -7.5, 7.5, 7.5),
                   int((-fx.ringPhase * 360.0 + 180.0) * 16), int(120 * 16));
    }
    // 辉光脉冲（#8）：随机闪辉——外圈亮环 + 球体白罩
    if (fx.pulseWave > 0.0) {
        QColor flash{fx.accent};
        flash.setAlpha(int(120 * fx.pulseWave));
        p->setPen(QPen(flash, 3));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(c, r + 5, r + 5);
        QColor veil{Qt::white};
        veil.setAlpha(int(26 * fx.pulseWave));
        p->setPen(Qt::NoPen);
        p->setBrush(veil);
        p->drawEllipse(realBall);
    }

    // ---- L3 球外扩散波（#16 呼吸波 / #18 爆闪；涟漪的球外部分不画） ----
    for (const DshBallFxState::Wave &w : fx.waves) {
        if (w.clipBall)
            continue;
        const double age = std::clamp(fx.t - w.spawnT, 0.0, w.dur);
        if (age < 0.0)
            continue;
        const double k = age / w.dur;
        const double e = 1.0 - (1.0 - k) * (1.0 - k); // easeOut
        QColor wc{fx.accent};
        wc.setAlpha(int(w.alpha0 * (1.0 - k)));
        p->setPen(QPen(wc, w.width * (1.0 - 0.5 * k)));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(w.center + QPointF(fx.magnetX, fx.magnetY),
                       r * 0.9 + w.maxR * e, r * 0.9 + w.maxR * e);
    }

    // ---- L4 球体（transform：悬停放大×按压×果冻/弹跳 scaleY） ----
    const double scale = fx.hoverScale * fx.pressScale;
    p->save();
    p->translate(c);
    p->scale(scale, scale * (1.0 + fx.jelly + fx.bounce));
    p->translate(-c);

    // 底色：深玻璃垂直渐变
    QColor top{fx.cardBg};
    top.setAlpha(228);
    QColor bottom{fx.cardBg};
    bottom.setAlpha(198);
    QLinearGradient grad(ball.topLeft(), ball.bottomLeft());
    grad.setColorAt(0.0, top);
    grad.setColorAt(1.0, bottom);
    p->setPen(Qt::NoPen);
    p->setBrush(grad);
    p->drawEllipse(ball);

    // #1 极光球：clip 球圆，两道锥形渐变色带反向流转
    p->save();
    QPainterPath auroraClip;
    auroraClip.addEllipse(ball);
    p->setClipPath(auroraClip);
    for (int layer = 0; layer < 2; ++layer) {
        const double phase = fx.auroraPhase + layer * 0.37;
        QConicalGradient aurora(ball.center(), phase * 360.0 - layer * 140.0);
        const int a = 62 - layer * 18;
        aurora.setColorAt(0.00, auroraColor(layer, a));
        aurora.setColorAt(0.18, auroraColor(layer + 1, a / 2));
        aurora.setColorAt(0.40, QColor(0, 0, 0, 0));
        aurora.setColorAt(0.60, auroraColor(layer + 2, a / 3));
        aurora.setColorAt(0.80, QColor(0, 0, 0, 0));
        aurora.setColorAt(1.00, auroraColor(layer, a));
        p->setPen(Qt::NoPen);
        p->setBrush(QBrush(aurora));
        p->drawEllipse(ball);
    }
    // #2 能量核心：球心径向光斑随呼吸微脉动
    const double coreK = 0.9 + 0.12 * std::sin(fx.t * 2.2);
    QRadialGradient core(ball.center(), r * 0.62 * coreK);
    QColor coreC{fx.accent};
    coreC.setAlpha(int(58 + 30 * std::sin(fx.t * 2.2)));
    core.setColorAt(0.0, coreC);
    coreC.setAlpha(0);
    core.setColorAt(1.0, coreC);
    p->setBrush(QBrush(core));
    p->drawEllipse(ball);
    p->restore();

    // #5 镜面高光：顶部高光弧随鼠标位置微游移（非悬停缓慢自摆）
    {
        double sway = std::sin(fx.t * 0.9) * r * 0.10; // 自摆
        if (fx.hover)
            sway += (fx.mousePos.x() - c.x()) / r * r * 0.12;
        const QPointF sheenC(ball.center().x() + sway, ball.top() + r * 0.42);
        QColor sheen{Qt::white};
        sheen.setAlpha(fx.dark ? 30 : 90);
        p->setBrush(sheen);
        p->setPen(Qt::NoPen);
        p->drawEllipse(sheenC, r * 0.70, r * 0.52);
    }

    // 边框（悬停 accent）
    QColor border = fx.hover ? QColor(fx.accent) : QColor(fx.stroke);
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(border, 1.2));
    p->drawEllipse(ball.adjusted(0, 0, -1, -1));

    // 中央字标（随球 transform 一同缩放）
    QFont f = p->font();
    f.setBold(true);
    f.setPixelSize(24);
    p->setFont(f);
    p->setPen(QColor(fx.accent));
    p->drawText(ball, Qt::AlignCenter, QStringLiteral("G"));
    p->restore();

    // ---- L5 球面涟漪（#11 点击涟漪 / #12 悬停波光；clip 球圆） ----
    if (!fx.waves.isEmpty()) {
        p->save();
        QPainterPath clip;
        clip.addEllipse(realBall);
        p->setClipPath(clip);
        for (const DshBallFxState::Wave &w : fx.waves) {
            if (!w.clipBall)
                continue;
            const double age = fx.t - w.spawnT;
            if (age < 0.0)
                continue; // 错相未出生
            const double k = std::clamp(age / w.dur, 0.0, 1.0);
            const double e = 1.0 - (1.0 - k) * (1.0 - k);
            QColor wc{Qt::white};
            wc.setAlpha(int(w.alpha0 * (1.0 - k)));
            p->setPen(QPen(wc, w.width * (1.0 - 0.5 * k)));
            p->setBrush(Qt::NoBrush);
            p->drawEllipse(w.center + QPointF(fx.magnetX, fx.magnetY),
                           w.maxR * e, w.maxR * e);
        }
        p->restore();
    }

    // ---- L6 星尘环绕（#9）+ 汇聚/发散（#10） ----
    for (const DshBallFxState::Dust &d : fx.dust) {
        const double ang = d.phase + fx.t * d.speed;
        const double orbit = r * d.orbit * fx.dustRadius;
        const QPointF pos = c + QPointF(std::cos(ang) * orbit, std::sin(ang) * orbit);
        const double tw = 0.45 + 0.55 * (0.5 + 0.5 * std::sin(fx.t * d.twinkle + d.phase));
        QColor dc = (d.speed < 0 ? auroraColor(0, int(150 * tw)) : QColor(fx.accent));
        dc.setAlpha(int(150 * tw));
        p->setPen(Qt::NoPen);
        p->setBrush(dc);
        p->drawEllipse(pos, d.size, d.size);
    }
}

} // namespace poptools
