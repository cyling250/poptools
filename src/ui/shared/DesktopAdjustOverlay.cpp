#include "DesktopAdjustOverlay.h"

#include "ui/ThemeColors.h" // overlay::warmAccent（T-071 橙收敛）

#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

namespace poptools {

namespace {
QRect divRect(const QRect &r, qreal dpr)
{
    return QRect(qRound(r.left() / dpr), qRound(r.top() / dpr), qRound(r.width() / dpr),
                 qRound(r.height() / dpr));
}
QRect mulRect(const QRect &r, qreal dpr)
{
    return QRect(qRound(r.left() * dpr), qRound(r.top() * dpr), qRound(r.width() * dpr),
                 qRound(r.height() * dpr));
}
} // namespace

DesktopAdjustOverlay::DesktopAdjustOverlay(const QRect &calendarRectPhysical,
                                           const QRect &screenPhysical,
                                           const QRect &screenLogical,
                                           const QList<QRect> &screenRectsLogical, qreal dpr,
                                           std::function<QImage()> previewProvider,
                                           std::function<void(const QRect &)> onChangedPhysical,
                                           std::function<void(const QRect &)> onConfirmedPhysical,
                                           QWidget *parent)
    : QWidget(parent, Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
    , m_preview(std::move(previewProvider))
    , m_screenRects(screenRectsLogical)
    , m_changed(std::move(onChangedPhysical))
    , m_confirmed(std::move(onConfirmedPhysical))
    , m_dpr(dpr)
    , m_screenNative(screenPhysical)
    , m_nativeOrigin(screenPhysical.topLeft())
    , m_logicalOrigin(screenLogical.topLeft())
    , m_screen(screenLogical)
    , m_inner(nativeToLogical(calendarRectPhysical))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    // 初始窗口覆盖 inner 中心所在的屏
    m_windowRect = m_screen;
    for (const QRect &r : m_screenRects)
        if (r.contains(m_inner.center()))
            m_windowRect = r;
    updateBoundsFromInner();
}

QRect DesktopAdjustOverlay::nativeToLogical(const QRect &physical) const
{
    // 虚拟桌面物理 → 所在屏 Qt 逻辑全局：本地物理 ÷ dpr 后平移到逻辑原点
    return divRect(physical.translated(-m_nativeOrigin), m_dpr).translated(m_logicalOrigin);
}

QRect DesktopAdjustOverlay::logicalToNative(const QRect &logical) const
{
    return mulRect(logical.translated(-m_logicalOrigin), m_dpr).translated(m_nativeOrigin);
}

// 窗口固定覆盖 inner 中心所在的屏：拖动中窗口零移动——半透明窗口每帧移动
// 经过鼠标位置时 Windows 会合成新的 mouse 事件喂回队列，事件洪水随拖动时长
// 累积（虚线框/预览一起滞后=不跟手、回放抖动=闪烁，越拖越明显）。虚线框/
// 预览作为窗口内容以局部坐标绘制，仅跨屏瞬间切换一次窗口。
void DesktopAdjustOverlay::updateBoundsFromInner()
{
    const QPoint center = m_inner.center();
    if (!m_windowRect.contains(center)) {
        for (const QRect &r : m_screenRects) {
            if (r.contains(center)) {
                m_windowRect = r;
                break;
            }
        }
    }
    setGeometry(m_windowRect);
    update(); // 预览日历与虚线框跟随 inner 重绘（窗口不动，纯内容重绘）
}

void DesktopAdjustOverlay::paintEvent(QPaintEvent *)
{
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    g.setCompositionMode(QPainter::CompositionMode_Source);
    g.fillRect(rect(), QColor(0, 0, 0, 120));
    g.setCompositionMode(QPainter::CompositionMode_SourceOver);

    // 日历面板预览：与虚线框同帧同层，拖动零跨进程延迟（跟手根治）。
    // 预览帧四周含 margin 余量（阴影/发光），对齐 inner 左上收进 margin 绘制
    const QRect inner(m_inner.left() - x(), m_inner.top() - y(), m_inner.width(),
                      m_inner.height());
    if (m_preview) {
        const QImage pv = m_preview();
        if (!pv.isNull())
            g.drawImage(QPoint(inner.left() - m_previewMargin, inner.top() - m_previewMargin), pv);
    }

    QPen dashPen(QColor(255, 255, 255, 255), 1.3, Qt::DashLine);
    g.setPen(dashPen);
    g.setBrush(Qt::NoBrush);
    g.drawRect(QRectF(inner));

    // 确认按钮（✓）居虚线框右上角；无取消语义——拖到哪里算哪里
    m_checkRect = QRect(inner.right() - 34, inner.top() + 8, 24, 24);
    g.setPen(Qt::NoPen);
    g.setBrush(QColor(18, 20, 26, 255));
    g.drawRect(m_checkRect);
    QPen checkPen(overlay::warmAccent(), 2); // T-071 收编：散写橙 → 具名语义色
    checkPen.setCapStyle(Qt::RoundCap);
    checkPen.setJoinStyle(Qt::RoundJoin);
    g.setPen(checkPen);
    g.drawPolyline(QVector<QPoint>{ QPoint(m_checkRect.left() + 6, m_checkRect.top() + 12),
                                    QPoint(m_checkRect.left() + 11, m_checkRect.top() + 17),
                                    QPoint(m_checkRect.left() + 19, m_checkRect.top() + 7) });

    QFont font;
    font.setFamilies({ QStringLiteral("Microsoft YaHei UI") });
    font.setPixelSize(12);
    g.setFont(font);
    g.setPen(QColor(240, 240, 240));
    // 基线经 ascent 锚定（P-064 口径：+22 手算偏移在 DPR 下会漂）
    g.drawText(QPoint(inner.left() + 4, inner.top() + 10 + QFontMetrics(font).ascent()),
               QStringLiteral("拖动边缘调整大小，拖动内部移动位置"));
}

void DesktopAdjustOverlay::keyPressEvent(QKeyEvent *event)
{
    // 无取消语义：✓/Enter/Esc 均为就地确认（拖到哪里算哪里）
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return
        || event->key() == Qt::Key_Enter) {
        if (m_confirmed)
            m_confirmed(logicalToNative(m_inner));
        close();
    }
    QWidget::keyPressEvent(event);
}

int DesktopAdjustOverlay::hitZone(const QPoint &localPt) const
{
    // rlqd 以屏幕坐标做边缘判定；此处等效为窗口局部坐标
    constexpr int edge = 14;
    int zone = 0;
    if (std::abs(localPt.x() - (m_inner.left() - x())) <= edge)
        zone |= 1;
    if (std::abs(localPt.x() - (m_inner.left() - x() + m_inner.width())) <= edge)
        zone |= 2;
    if (std::abs(localPt.y() - (m_inner.top() - y())) <= edge)
        zone |= 4;
    if (std::abs(localPt.y() - (m_inner.top() - y() + m_inner.height())) <= edge)
        zone |= 8;
    if (zone != 0)
        return zone;
    const QRect innerLocal(m_inner.left() - x(), m_inner.top() - y(), m_inner.width(),
                           m_inner.height());
    return innerLocal.contains(localPt) ? 0 : -1;
}

void DesktopAdjustOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (m_checkRect.contains(event->pos())) {
        if (m_confirmed)
            m_confirmed(logicalToNative(m_inner));
        close();
        return;
    }
    const int z = hitZone(event->pos());
    if (z < 0)
        return;
    m_zone = z;
    m_startRect = m_inner;
    m_startGlobal = event->globalPosition().toPoint();
    m_dragging = true;
    grabMouse();
}

void DesktopAdjustOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        const int z = hitZone(event->pos());
        switch (z) {
            case 1:
            case 2:
                setCursor(Qt::SizeHorCursor);
                break;
            case 4:
            case 8:
                setCursor(Qt::SizeVerCursor);
                break;
            case 5:
            case 10:
                setCursor(Qt::SizeFDiagCursor);
                break;
            case 6:
            case 9:
                setCursor(Qt::SizeBDiagCursor);
                break;
            case 0:
                setCursor(Qt::SizeAllCursor);
                break;
            default:
                unsetCursor();
                break;
        }
        return;
    }
    // 位移用全局坐标差值：窗口跨屏切换时局部坐标原点跳变（±1920px），若用
    // 局部坐标算位移，面板会在边界处反向跳回并被钳回旧屏（跨屏拖动卡死根因）；
    // 全局坐标与窗口位置解耦，跨屏连续
    const QPoint d = event->globalPosition().toPoint() - m_startGlobal;
    int l = m_startRect.left();
    int t = m_startRect.top();
    int r = m_startRect.left() + m_startRect.width();
    int b = m_startRect.top() + m_startRect.height();
    if (m_zone == 0) {
        l += d.x();
        r += d.x();
        t += d.y();
        b += d.y();
    } else {
        if (m_zone & 1)
            l += d.x();
        if (m_zone & 2)
            r += d.x();
        if (m_zone & 4)
            t += d.y();
        if (m_zone & 8)
            b += d.y();
    }
    constexpr int minW = 520, minH = 420;
    if (r - l < minW) {
        if (m_zone & 1)
            l = r - minW;
        else
            r = l + minW;
    }
    if (b - t < minH) {
        if (m_zone & 4)
            t = b - minH;
        else
            b = t + minH;
    }
    l = std::max(m_screen.left(), l);
    t = std::max(m_screen.top(), t);
    r = std::min(m_screen.left() + m_screen.width(), r);
    b = std::min(m_screen.top() + m_screen.height(), b);
    if (r - l < minW)
        r = std::min(m_screen.left() + m_screen.width(), l + minW);
    if (b - t < minH)
        b = std::min(m_screen.top() + m_screen.height(), t + minH);
    m_inner = QRect(l, t, r - l, b - t);
    updateBoundsFromInner();
    if (m_changed)
        m_changed(logicalToNative(m_inner));
}

void DesktopAdjustOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    if (m_dragging) {
        m_dragging = false;
        releaseMouse();
        if (m_changed)
            m_changed(logicalToNative(m_inner));
    }
}


} // namespace poptools
