#include "ScreenCapturer.h"

#include <QGuiApplication>
#include <QScreen>
#include <QPainter>

namespace poptools {

QRect ScreenCapturer::screenPhysicalRect(const QScreen *s)
{
    if (!s)
        return {};
    const qreal dpr = s->devicePixelRatio();
    const QRect lg = s->geometry();
    const QPoint tl(qRound(lg.left() * dpr), qRound(lg.top() * dpr));
    const QPoint br(qRound((lg.right() + 1) * dpr), qRound((lg.bottom() + 1) * dpr));
    return QRect(tl, QSize(br.x() - tl.x(), br.y() - tl.y()));
}

CaptureResult ScreenCapturer::capture()
{
    CaptureResult result;

    // 计算所有屏幕合并后的虚拟桌面区域（逻辑坐标：窗口/鼠标/选区均用逻辑坐标）
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *s : screens)
        result.desktop = result.desktop.united(s->geometry());

    // DPI 修复：grabWindow 返回的是物理像素图。此前按逻辑尺寸创建画布，
    // 物理图被 drawPixmap 压缩 → 截图掉分辨率。现按物理分辨率拼接
    for (QScreen *s : screens)
        result.physDesktop = result.physDesktop.united(screenPhysicalRect(s));

    result.screen = QPixmap(result.physDesktop.size());
    result.screen.fill(Qt::black);
    {
        QPainter p(&result.screen);
        for (QScreen *s : screens) {
            QPixmap shot = s->grabWindow(0);
            shot.setDevicePixelRatio(1.0); // 物理像素 1:1 拼接（否则按 dpr 缩小）
            p.drawPixmap(screenPhysicalRect(s).topLeft() - result.physDesktop.topLeft(), shot);
        }
    }
    return result;
}

QPoint ScreenCapturer::toPhysical(const QPoint &logicalGlobal)
{
    QScreen *s = nullptr;
    for (QScreen *sc : QGuiApplication::screens()) {
        // 半开区间判定（右/下边界归属本屏）：QRect::contains 不含右/下端点，
        // 屏幕边界上的像素点会漏判落到主屏，异 DPI 多屏下产生 1px 级错位
        const QRect g = sc->geometry();
        if (logicalGlobal.x() >= g.left() && logicalGlobal.x() < g.left() + g.width()
            && logicalGlobal.y() >= g.top() && logicalGlobal.y() < g.top() + g.height()) {
            s = sc;
            break;
        }
    }
    if (!s)
        s = QGuiApplication::primaryScreen();
    const qreal dpr = s->devicePixelRatio();
    const QRect lg = s->geometry();
    // 与 screenPhysicalRect 一致：屏幕物理原点 + 屏幕内逻辑偏移×dpr
    return QPoint(qRound(lg.left() * dpr) + qRound((logicalGlobal.x() - lg.left()) * dpr),
                  qRound(lg.top() * dpr) + qRound((logicalGlobal.y() - lg.top()) * dpr));
}

QRect ScreenCapturer::toPhysicalRect(const QRect &logicalGlobal)
{
    // 逻辑全局矩形（Qt 矩形右/下边界含端点）→ 物理像素矩形
    const QPoint tl = toPhysical(logicalGlobal.topLeft());
    const QPoint br = toPhysical(logicalGlobal.bottomRight() + QPoint(1, 1));
    return QRect(tl, br - QPoint(1, 1));
}

} // namespace poptools
