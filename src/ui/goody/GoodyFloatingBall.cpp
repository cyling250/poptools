// T-050 阶段一：Goody 桌面悬浮球实现（贴底驻留/手势判别/呼吸光效/主题联动）
#include "GoodyFloatingBall.h"

#include "GoodyBallLogic.h"
#include "core/settings/Settings.h"
#include "ui/ThemeColors.h"
#include "ui/shared/DesktopStack.h"

#include <QEnterEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QScreen>
#include <QStyleHints>
#include <QTimer>

#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace poptools {

namespace {
constexpr int kBallWindowSize = 68; // 窗体（球 + 光晕余量）
constexpr int kBallDiameter = 52;   // 球直径
constexpr int kBreathPeriodMs = 1200; // 呼吸周期（需求单定案）
constexpr qreal kTwoPi = 6.283185307179586;

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

GoodyFloatingBall::GoodyFloatingBall(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("goodyBall");
    // 复用现有资源：resources 无 goody 专属图标，窗口图标取应用方砖；
    // 球面中央字标自绘 "G"（见 paintEvent）
    setWindowIcon(QIcon(QStringLiteral(":/app_icon.svg")));
    // 窗口纪律：无边框工具窗、透明背景、显示不激活
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedSize(kBallWindowSize, kBallWindowSize);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Goody 管家：单击打开对话，拖动调整位置"));

    // 跟随系统明暗主题（TranslateCard.cpp 先例）
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, &GoodyFloatingBall::applyTheme);
    applyTheme();
}

void GoodyFloatingBall::applyTheme()
{
    // P-023 双通道在本件的落法：球体完全 paintEvent 自绘，无 QSS 通道
    //（不存在"样式表失效落黑底"路径）；调色板钉扎作第二通道兜底——
    // tooltip 等经调色板取色的路径在 QSS/主题异常时仍可读
    const ThemeColors c = uiThemeColors(systemSchemeDark());
    QPalette pal = palette();
    for (const QPalette::ColorGroup group :
         {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        pal.setBrush(group, QPalette::Window, QColor(c.cardBg));
        pal.setBrush(group, QPalette::WindowText, QColor(c.body));
        pal.setBrush(group, QPalette::Highlight, QColor(c.accent));
    }
    setPalette(pal);
    update(); // 窗体即球矩形（68px），全量重绘无伤
}

// ---- 工作呼吸光效 ----

void GoodyFloatingBall::setWorking(bool on)
{
    if (m_working == on)
        return;
    m_working = on;
    if (on)
        startBreath();
    else
        stopBreath();
}

void GoodyFloatingBall::startBreath()
{
    // T-050-FIX（P3-1）：可见性守卫——聊天窗开着（球已隐没）期间
    // workingChanged(true) 到达时不得在隐藏窗口上空转动画；
    // 显示即恢复由 showEvent 的 m_working 分支负责
    if (!isVisible())
        return;
    if (!m_breath) {
        m_breath = new QPropertyAnimation(this, "glowPhase", this);
        m_breath->setStartValue(0.0);
        m_breath->setEndValue(1.0);
        m_breath->setDuration(kBreathPeriodMs);
        m_breath->setLoopCount(-1); // 工作期间持续呼吸
    }
    m_glowPhase = 0.0;
    m_breath->start();
}

void GoodyFloatingBall::stopBreath()
{
    if (m_breath)
        m_breath->stop();
    m_glowPhase = 0.0;
    update(glowDirtyRect()); // 清掉残留光晕（脏矩形）
}

void GoodyFloatingBall::setGlowPhase(qreal phase)
{
    m_glowPhase = phase;
    update(glowDirtyRect()); // 性能红线：脏矩形重绘，禁全窗/全屏 update
}

// ---- 手势（分类器状态机，goodyball::GestureClassifier 可单测） ----

void GoodyFloatingBall::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_gesture.beginPress(event->globalPosition().toPoint());
        m_winPressOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void GoodyFloatingBall::mouseMoveEvent(QMouseEvent *event)
{
    m_gesture.addMove(event->globalPosition().toPoint());
    if (m_gesture.dragging()) // 超阈值闩锁后才跟手，避免微动即拖
                              //（未闩锁期间 addMove 只累计位移，松开仍判单击）
        move(event->globalPosition().toPoint() - m_winPressOffset);
    event->accept();
}

void GoodyFloatingBall::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    const goodyball::Gesture g = m_gesture.endRelease();
    if (g == goodyball::Gesture::Click) {
        emit clicked();
    } else if (g == goodyball::Gesture::Drag) {
        // T-050-FIX（P3-2）：松开先钳进可用屏再落盘（与 applySavedPosition
        // 同一 clampBallPos）——拖出屏幕/压任务栏的落点否则会话内滞留，
        // 重启才被钳回；窗口同步移到钳制位，视觉位置与持久化值一致
        const QPoint clamped =
            goodyball::clampBallPos(pos(), size(), visibleScreenRects());
        if (clamped != pos())
            move(clamped);
        emit positionChanged(clamped); // 松开写回位置（模块持久化）
    }
    event->accept();
}

void GoodyFloatingBall::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    update(ballRect()); // 悬停描边反馈（无定时器）
    QWidget::enterEvent(event);
}

void GoodyFloatingBall::leaveEvent(QEvent *event)
{
    m_hover = false;
    update(ballRect());
    QWidget::leaveEvent(event);
}

// ---- 驻桌（desktopstack：Progman 上方 + 1s 守护归位） ----

void GoodyFloatingBall::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_working)
        startBreath(); // 隐藏期间被停表的呼吸，显示即恢复
    // 显示完成后贴底（winId 就绪后再 attach）
    QTimer::singleShot(0, this, [this] {
        if (isVisible())
            desktopstack::attach(this, desktopstack::Layer::Ball);
    });
}

void GoodyFloatingBall::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    // 守护表配对注销（R3 #19）：否则守护定时器为不可见窗口空转；
    // 隐藏即停呼吸（聊天窗隐没期间零动画）
    desktopstack::detach(this);
    if (m_breath && m_breath->state() == QAbstractAnimation::Running)
        m_breath->stop();
}

// P-031：点击不激活窗口（抑制 Windows 默认抬升，避免盖住正常应用
// 窗口后又被守护归位形成横跳）——只拦 WM_MOUSEACTIVATE，鼠标按键
// 消息照常到达 Qt 事件层，单击/拖动手势不受影响
bool GoodyFloatingBall::nativeEvent(const QByteArray &eventType, void *message,
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

QRect GoodyFloatingBall::ballRect() const
{
    const int off = (kBallWindowSize - kBallDiameter) / 2;
    return QRect(off, off, kBallDiameter, kBallDiameter);
}

QRect GoodyFloatingBall::glowDirtyRect() const
{
    // 光晕外沿：半径 r+4 + 3px 笔宽 + 抗锯齿余量
    return ballRect().adjusted(-6, -6, 6, 6);
}

void GoodyFloatingBall::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const ThemeColors c = uiThemeColors(systemSchemeDark());
    const QRect ball = ballRect();
    const QPointF center = ball.center();
    const qreal r = ball.width() / 2.0;

    // 呼吸光晕（仅工作态）：accent 圆环描边，alpha 随相位余弦呼吸
    if (m_working) {
        const qreal wave = 0.5 - 0.5 * std::cos(kTwoPi * m_glowPhase); // 0→1→0
        QColor glow{QColor(c.accent)};
        glow.setAlpha(int(45 + 75 * wave));
        QPen pen(glow, 3);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(center, r + 4, r + 4);
    }

    // 玻璃感球体：cardBg 半透明垂直渐变（底色随主题表）+ 顶部高光
    QColor top{QColor(c.cardBg)};
    top.setAlpha(225);
    QColor bottom{QColor(c.cardBg)};
    bottom.setAlpha(195);
    QLinearGradient grad(ball.topLeft(), ball.bottomLeft());
    grad.setColorAt(0.0, top);
    grad.setColorAt(1.0, bottom);
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawEllipse(ball);
    // 顶部高光弧（玻璃感的来源：上半一抹亮）
    QColor sheen = QColor(Qt::white);
    sheen.setAlpha(c.dark ? 28 : 90);
    p.setBrush(sheen);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(center.x(), ball.top() + r * 0.42), r * 0.72, r * 0.55);

    // 边框：悬停强调色，常态主题灰（lighter 一档保证深色底可见）
    QColor border = m_hover ? QColor(c.accent) : QColor(c.cardBorder);
    if (!m_hover && c.dark)
        border = border.lighter(125);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(border, 1.2));
    p.drawEllipse(ball.adjusted(0, 0, -1, -1));

    // 中央字标 "G"：浅色主题 accent 原色，深色主题 accent 提亮
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(24);
    p.setFont(f);
    QColor letter{QColor(c.accent)};
    if (c.dark)
        letter = letter.lighter(150);
    p.setPen(letter);
    p.drawText(ball, Qt::AlignCenter, QStringLiteral("G"));
}

// ---- 位置 ----

void GoodyFloatingBall::applySavedPosition()
{
    const QList<QRect> screens = visibleScreenRects(); // 排除任务栏（共用取法）
    const QSize sz = size();
    bool ok = false;
    QPoint pos = goodyball::ballPosFromString(Settings::instance().assistantBallPos(), &ok);
    if (!ok) // 未记忆/坏值：可用区右上角默认位
        pos = goodyball::defaultBallPos(QGuiApplication::primaryScreen()->availableGeometry(), sz);
    move(goodyball::clampBallPos(pos, sz, screens)); // 启动钳制进可见屏幕
}

} // namespace poptools
