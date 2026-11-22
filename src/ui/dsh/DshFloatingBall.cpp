// T-050 阶段一：Goody 桌面悬浮球实现（贴底驻留/手势判别/主题联动）
// T-076（2026-11-22）：渲染与动效全量委托 DshBallFx（18 项动效系统），
// 本件瘦身为接线层——主时钟/事件/位置/驻桌。
#include "DshFloatingBall.h"

#include "DshBallLogic.h"
#include "core/settings/Settings.h"
#include "ui/ThemeColors.h" // systemSchemeDark
#include "ui/theming/FluentTheme.h"
#include "ui/shared/DesktopStack.h"

#include <QCursor>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QStyleHints>
#include <QTimer>

#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace poptools {

namespace {
constexpr int kBallWindowSize = 88; // 窗体（球 52 + 光效外沿余量 18：波/星尘/彗尾）
constexpr int kBallDiameter = 52;   // 球直径
constexpr int kFxFps = 30;          // 动效主时钟帧率（88px 小窗，负载极低）
constexpr int kMagnetRadius = 120;  // 磁吸感知半径（鼠标接近时微移）
constexpr double kMagnetMaxPx = 3.0;
constexpr double kFxStep = 1.0 / 30.0;

// 可用屏几何列表（排除任务栏）——applySavedPosition 与拖动松开钳制
// 共用同一取法，防两处漂移（T-050-FIX P3-2 提取）
QList<QRect> visibleScreenRects()
{
    QList<QRect> screens;
    const QList<QScreen *> all = QGuiApplication::screens();
    screens.reserve(all.size());
    for (QScreen *s : all)
        screens << s->availableGeometry(); // 排除任务栏
    return screens;
}
} // namespace

DshFloatingBall::DshFloatingBall(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("goodyBall");
    // 复用现有资源：resources 无 goody 专属图标，窗口图标取应用方砖；
    // 球面中央字标自绘 "G"（DshBallFx 内绘制）
    setWindowIcon(QIcon(QStringLiteral(":/app_icon.svg")));
    // 窗口纪律：无边框工具窗、透明背景、显示不激活
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedSize(kBallWindowSize, kBallWindowSize);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("DeepSeek Harness：单击打开对话，拖动调整位置"));

    // 跟随系统明暗主题（TranslateCard.cpp 先例）
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, &DshFloatingBall::applyTheme);
    applyTheme();

    fxSeedDust(m_fx); // 星尘轨道初始化（#9，一次）

    // 30fps 主时钟（T-076）：可见期间推进全部动效相位；磁吸用 QCursor::pos()
    // 低频轮询（窗未悬停也能感知鼠标接近；33ms 一次零成本）
    m_fxClock = new QTimer(this);
    m_fxClock->setInterval(1000 / kFxFps);
    connect(m_fxClock, &QTimer::timeout, this, &DshFloatingBall::onFxTick);

    // 屏幕布局变化即时重钳制（T-076）：拔屏/主屏切换/改分辨率后旧记忆坐标
    // 可能落在已消失的屏幕区间（球缺角/失踪），applySavedPosition 幂等
    const auto reclamp = [this] {
        if (isVisible())
            applySavedPosition();
    };
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, reclamp);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, reclamp);
    for (QScreen *s : QGuiApplication::screens())
        connect(s, &QScreen::geometryChanged, this, reclamp);
}

void DshFloatingBall::applyTheme()
{
    const FluentTokens t = fluentTheme(systemSchemeDark());
    // 动效系统调色（DshBallFx 渲染唯一取色处）
    m_fx.accent = QColor(t.accent);
    m_fx.cardBg = QColor(t.bgCard);
    m_fx.stroke = QColor(t.cardStroke);
    m_fx.dark = t.dark;
    // P-023 双通道在本件的落法：球体完全自绘，无 QSS 通道；调色板钉扎
    // 兜底 tooltip 等经调色板取色的路径
    QPalette pal = palette();
    for (const QPalette::ColorGroup group :
         {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        pal.setBrush(group, QPalette::Window, QColor(t.bgCard));
        pal.setBrush(group, QPalette::WindowText, QColor(t.textPrimary));
        pal.setBrush(group, QPalette::Highlight, QColor(t.accent));
    }
    setPalette(pal);
    update(glowDirtyRect());
}

// ---- 主时钟 ----

void DshFloatingBall::onFxTick()
{
    // #14 磁吸目标：QCursor 逻辑全局坐标 vs 窗心，接近半径内朝向偏移 ≤3px
    const QPoint cursor = QCursor::pos();
    const QPointF center = frameGeometry().center();
    const QLineF toCursor(center, cursor);
    if (toCursor.length() >= 1.0 && toCursor.length() < kMagnetRadius
        && !m_fx.dragging) {
        const double k = kMagnetMaxPx * (1.0 - toCursor.length() / kMagnetRadius);
        m_fx.magnetTargetX = toCursor.dx() / toCursor.length() * k;
        m_fx.magnetTargetY = toCursor.dy() / toCursor.length() * k;
    } else {
        m_fx.magnetTargetX = 0.0;
        m_fx.magnetTargetY = 0.0;
    }
    tickBallFx(m_fx, kFxStep);
    update(glowDirtyRect());
}

void DshFloatingBall::startFxClock()
{
    m_fxClock->start();
}

void DshFloatingBall::stopFxClock()
{
    m_fxClock->stop();
}

// ---- 工作状态 ----

void DshFloatingBall::setWorking(bool on)
{
    if (m_fx.working == on)
        return;
    m_fx.working = on;
    if (!on)
        fxKickBounce(m_fx); // #18 完成弹跳+爆闪（工作结束反馈）
}

// ---- 手势（分类器状态机，dshball::GestureClassifier 可单测） ----

void DshFloatingBall::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_fx.pressed = true;
        m_fx.mousePos = event->pos();
        fxSpawnRipple(m_fx, event->pos()); // #11 点击涟漪（按下瞬间球面水波）
        m_gesture.beginPress(event->globalPosition().toPoint());
        m_winPressOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void DshFloatingBall::mouseMoveEvent(QMouseEvent *event)
{
    m_fx.mousePos = event->pos();
    m_gesture.addMove(event->globalPosition().toPoint());
    m_fx.dragging = m_gesture.dragging(); // #15 拖动中彗尾采样
    if (m_gesture.dragging()) // 超阈值闩锁后才跟手，避免微动即拖
                              //（未闩锁期间 addMove 只累计位移，松开仍判单击）
        move(event->globalPosition().toPoint() - m_winPressOffset);
    event->accept();
}

void DshFloatingBall::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_fx.pressed = false;
    const dshball::Gesture g = m_gesture.endRelease();
    m_fx.dragging = false;
    if (g == dshball::Gesture::Click) {
        emit clicked();
    } else if (g == dshball::Gesture::Drag) {
        fxKickJelly(m_fx, 2.6); // #4 果冻回弹（松手冲击）
        // T-050-FIX（P3-2）：松开先钳进可用屏再落盘（与 applySavedPosition
        // 同一 clampBallPos）——拖出屏幕/压任务栏的落点否则会话内滞留，
        // 重启才被钳回；窗口同步移到钳制位，视觉位置与持久化值一致
        const QPoint clamped =
            dshball::clampBallPos(pos(), size(), visibleScreenRects());
        if (clamped != pos())
            move(clamped);
        emit positionChanged(clamped); // 松开写回位置（模块持久化）
    }
    event->accept();
}

void DshFloatingBall::enterEvent(QEnterEvent *event)
{
    m_fx.hover = true;
    m_fx.mousePos = mapFromGlobal(QCursor::pos());
    QWidget::enterEvent(event);
}

void DshFloatingBall::leaveEvent(QEvent *event)
{
    m_fx.hover = false;
    QWidget::leaveEvent(event);
}

// ---- 驻桌（desktopstack：Progman 上方 + 1s 守护归位） ----

void DshFloatingBall::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    startFxClock(); // 显示即启动效时钟（隐藏停钟零开销）
    // 显示完成后贴底（winId 就绪后再 attach）
    QTimer::singleShot(0, this, [this] {
        if (isVisible())
            desktopstack::attach(this, desktopstack::Layer::Ball);
    });
}

void DshFloatingBall::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    // 守护表配对注销（R3 #19）：否则守护定时器为不可见窗口空转；
    // 隐藏即停时钟（聊天窗隐没期间零动画）
    desktopstack::detach(this);
    stopFxClock();
}

// P-031：点击不激活窗口（抑制 Windows 默认抬升，避免盖住正常应用
// 窗口后又被守护归位形成横跳）——只拦 WM_MOUSEACTIVATE，鼠标按键
// 消息照常到达 Qt 事件层，单击/拖动手势不受影响
bool DshFloatingBall::nativeEvent(const QByteArray &eventType, void *message,
                                    qintptr *result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_MOUSEACTIVATE) {
            *result = MA_NOACTIVATE;
            return true;
        }
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

// ---- 绘制 ----

QRect DshFloatingBall::ballRect() const
{
    const int off = (kBallWindowSize - kBallDiameter) / 2;
    return QRect(off, off, kBallDiameter, kBallDiameter);
}

QRect DshFloatingBall::glowDirtyRect() const
{
    // 光效外沿最大 r+13（爆闪/呼吸波/彗尾弧）+ 磁吸 3px + 抗锯齿余量
    return ballRect().adjusted(-17, -17, 17, 17);
}

void DshFloatingBall::paintEvent(QPaintEvent * /*event*/)
{
    m_fx.ball = QRectF(ballRect()); // 窗体几何 → 动效系统（每帧同步）
    QPainter p(this);
    paintBallFx(&p, m_fx);
}

// ---- 位置 ----

void DshFloatingBall::applySavedPosition()
{
    const QList<QRect> screens = visibleScreenRects(); // 排除任务栏（共用取法）
    const QSize sz = size();
    bool ok = false;
    QPoint pos = dshball::ballPosFromString(Settings::instance().dshBallPos(), &ok);
    if (!ok) // 未记忆/坏值：可用区右上角默认位
        pos = dshball::defaultBallPos(QGuiApplication::primaryScreen()->availableGeometry(), sz);
    move(dshball::clampBallPos(pos, sz, screens)); // 启动钳制进可见屏幕
}

} // namespace poptools
