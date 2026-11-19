#include "ToggleSwitch.h"

#include "ui/ThemeColors.h"
#include "ui/theming/FluentTheme.h"

#include <QPainter>
#include <QStyleHints>

namespace poptools {

namespace {
constexpr int kTrackW = 40;          // 胶囊宽
constexpr int kTrackH = 20;          // 胶囊高
constexpr int kThumbD = 14;          // 滑块直径
constexpr int kAnimMs = 120;         // 位移动画时长（Win11 手感）
} // namespace

ToggleSwitch::ToggleSwitch(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(kTrackW, kTrackH);

    m_anim.setDuration(kAnimMs);
    m_anim.setStartValue(0.0);
    m_anim.setEndValue(1.0);
    // toggled 后动画从当前位置滑向目标态（双向复用同一动画）
    connect(this, &QAbstractButton::toggled, this, [this](bool checked) {
        m_anim.stop();
        m_anim.setStartValue(m_anim.currentValue().isValid()
                                 ? m_anim.currentValue().toReal()
                                 : (checked ? 0.0 : 1.0));
        m_anim.setEndValue(checked ? 1.0 : 0.0);
        m_anim.start();
    });
}

qreal ToggleSwitch::thumbPos() const
{
    if (m_anim.state() == QVariantAnimation::Running)
        return m_anim.currentValue().toReal();
    return isChecked() ? 1.0 : 0.0;
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);

    const FluentTokens t = fluentTheme(systemSchemeDark());

    // 胶囊：选中 accent 实底；未选中 hover 时底色加深一档便于发现可点
    QRectF track(0.5, 0.5, kTrackW - 1.0, kTrackH - 1.0);
    QColor trackColor;
    if (isChecked()) {
        trackColor = QColor(m_hover ? t.accentHover : t.accent);
    } else {
        trackColor = QColor(t.ctrlBorder);
        if (m_hover)
            trackColor = QColor(t.ctrlHover);
    }
    if (!isEnabled()) {
        trackColor = QColor(t.bgSubtle);
    }
    g.setPen(Qt::NoPen);
    g.setBrush(trackColor);
    g.drawRoundedRect(track, kTrackH / 2.0 - 0.5, kTrackH / 2.0 - 0.5);

    // 白色滑块：行程 = 宽 - 高（左右各留半高内边距），关闭态微缩进
    const qreal travel = kTrackW - kTrackH;
    const qreal x = kTrackH / 2.0 - kThumbD / 2.0 + thumbPos() * travel;
    QRectF thumb(x, (kTrackH - kThumbD) / 2.0, kThumbD, kThumbD);
    g.setBrush(isEnabled() ? QColor(t.onAccent) : QColor(t.textDisabled));
    g.drawEllipse(thumb);
}

void ToggleSwitch::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(event);
}

void ToggleSwitch::leaveEvent(QEvent *event)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(event);
}

} // namespace poptools
