#pragma once

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QVector>

class QPainter;

namespace poptools {

// ============================================================
// GoodyBallFx：悬浮球动效系统（T-076 全量动效批，用户拍板"全要"）。
//
// 结构：GoodyBallFxState（全部相位/波列表/弹簧状态）+ 纯函数 tick()/paint()
// ——无 QObject 依赖、状态可快照比对，便于将来单测。GoodyFloatingBall 只
// 负责主时钟（30fps tick→脏矩形重绘）与事件接线（按下/悬停/拖动/working）。
//
// 动效分层（paint 由底到顶）：
//   L1 拖动彗尾残影           （拖动中）
//   L2 光环系统：待机彗尾弧+双层反转环 / working 呼吸双层 / 辉光脉冲
//   L3 扩散波：呼吸波发射 / 完成爆闪
//   L4 球体：底色渐变+极光带+能量核心+镜面高光（果冻/放大/磁吸 transform）
//   L5 球内波纹：点击涟漪 / 悬停波光（clip 球圆）
//   L6 星尘环绕（汇聚/发散随 hover/working）
//   L7 边框 + G 字标
//
// 几何纪律：窗体 88px（半宽 44），球 52（r=26）；所有光效外沿 ≤43——
// 超窗即光效缺圈（2026-11-22 缺角反馈的渲染侧纪律）。
// ============================================================

struct GoodyBallFxState
{
    // ---- 输入（窗体接线侧每帧/事件更新） ----
    QRectF ball;            // 球矩形（窗中心 52px）
    QColor accent;
    QColor cardBg;          // 球底色（深色玻璃）
    QColor stroke;          // 常态描边
    bool dark = true;
    bool working = false;
    bool hover = false;
    bool pressed = false;
    bool dragging = false;
    QPointF mousePos;       // 窗内鼠标（hover 期间有效，磁吸用窗外值由接线侧折算）

    // ---- 时间轴与相位（tick 推进） ----
    double t = 0.0;         // 累计秒
    double auroraPhase = 0.0;   // 极光旋转（working 时流速 ×3，#17 思考流光）
    double haloPhase = 0.0;     // 待机彗尾弧旋转（#6）
    double ringPhase = 0.0;     // 反转环相位（#7，负速反向）
    double breathPhase = 0.0;   // working 呼吸（#2/#16 节拍源）
    double lastBreathPhase = 0.0; // 呼吸回绕检测（发射呼吸波）
    double pulseCountdown = 3.0; // 辉光脉冲随机倒计时（#8）
    double pulseWave = 0.0;     // 脉冲强度（>0 衰减中）

    // ---- 弹簧量（tick 积分） ----
    double hoverScale = 1.0;    // #13 悬停放大（目标 hover?1.08:1.0）
    double pressScale = 1.0;    // #4 按压缩放（目标 pressed?0.92:(dragging?0.95:1)）
    double jelly = 0.0;         // #4 果冻回弹（松手阻尼振荡）
    double jellyVel = 0.0;
    double magnetX = 0.0;       // #14 磁吸微移当前值（指数逼近 target）
    double magnetY = 0.0;
    double magnetTargetX = 0.0; // 接线侧按窗外鼠标距离计算（≤3px）
    double magnetTargetY = 0.0;
    double bounce = 0.0;        // #18 完成弹跳 scaleY 偏移（阻尼振荡）
    double bounceVel = 0.0;
    double dustRadius = 1.0;    // #10 星尘轨道系数（hover→0.72 汇聚 / working→1.32 发散）

    // ---- 波纹圈（#11 点击涟漪 / #12 悬停波光 / #16 呼吸波 / #18 爆闪 共用） ----
    struct Wave
    {
        QPointF center;   // 波源（窗内坐标）
        double spawnT;    // 发射时刻（t）
        double maxR;      // 终止半径
        double dur;       // 生命周期秒
        double alpha0;    // 初始 alpha
        double width;     // 描边宽
        bool clipBall;    // true=clip 球圆（球面涟漪），false=球外自由扩散
    };
    QVector<Wave> waves;

    // ---- 拖动彗尾（#15）：最近球心采样（窗内坐标，限制偏移量保窗内可见） ----
    QVector<QPointF> trail;
    double trailTimer = 0.0;

    // ---- 星尘（#9）：轨道参数初始化由 seedDust() 完成 ----
    struct Dust
    {
        double orbit;   // 轨道半径（×r 的系数，1.0 = 贴球缘）
        double speed;   // 角速度 rad/s（可负=反向）
        double phase;   // 初始角
        double size;    // 粒子半径 px
        double twinkle; // 闪烁频率 rad/s
    };
    QVector<Dust> dust;
};

// 主时钟推进（dt 秒；接线侧 30fps 固定步进）：相位/弹簧/波生命周期/彗尾采样
void tickBallFx(GoodyBallFxState &fx, double dt);

// 事件触发（GoodyFloatingBall 接线侧调用）
void fxSeedDust(GoodyBallFxState &fx);                      // 星尘轨道初始化（构造调一次）
void fxSpawnRipple(GoodyBallFxState &fx, const QPointF &p); // #11 点击涟漪（按下点）
void fxKickJelly(GoodyBallFxState &fx, double strength);    // #4 松手果冻回弹冲击
void fxKickBounce(GoodyBallFxState &fx);                    // #18 完成弹跳+爆闪

// 分层渲染（paintEvent 委托；传入窗矩形以定位球）
void paintBallFx(QPainter *p, const GoodyBallFxState &fx);

} // namespace poptools
