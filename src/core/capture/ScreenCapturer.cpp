#include "ScreenCapturer.h"

#include <QGuiApplication>
#include <QScreen>
#include <QPainter>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <cstring>

namespace poptools {

// 线程池可用的纯 GDI 虚拟桌面截屏。SM_*VIRTUAL* 系列本就物理像素（进程为
// per-monitor DPI aware）；BitBlt 带 CAPTUREBLT 把分层窗口一并收进来，视觉
// 语义与 QScreen::grabWindow 对齐。DIB 32bpp 的字节序（BGRA）与 QImage ARGB32
// 在小端机内存布局一致，整块 memcpy 即得
bool ScreenCapturer::captureVirtualDesktop(QImage *out, QRect *physDesktopOut)
{
#ifdef Q_OS_WIN
    if (!out || !physDesktopOut)
        return false;
    const QRect phys(QPoint(GetSystemMetrics(SM_XVIRTUALSCREEN),
                            GetSystemMetrics(SM_YVIRTUALSCREEN)),
                     QSize(GetSystemMetrics(SM_CXVIRTUALSCREEN),
                           GetSystemMetrics(SM_CYVIRTUALSCREEN)));
    if (phys.isEmpty())
        return false;
    const HDC screenDc = GetDC(nullptr);
    const HDC memDc = screenDc ? CreateCompatibleDC(screenDc) : nullptr;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = phys.width();
    bmi.bmiHeader.biHeight = -phys.height(); // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    const HBITMAP bmp = memDc ? CreateDIBSection(memDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0)
                              : nullptr;
    bool ok = false;
    if (screenDc && memDc && bmp && bits) {
        const HGDIOBJ old = SelectObject(memDc, bmp);
        if (BitBlt(memDc, 0, 0, phys.width(), phys.height(), screenDc,
                   phys.left(), phys.top(), SRCCOPY | CAPTUREBLT)) {
            QImage img(phys.width(), phys.height(), QImage::Format_ARGB32);
            memcpy(img.bits(), bits,
                   size_t(phys.width()) * size_t(phys.height()) * 4);
            *out = img;
            *physDesktopOut = phys;
            ok = true;
        }
        SelectObject(memDc, old);
    }
    if (bmp)
        DeleteObject(bmp);
    if (memDc)
        DeleteDC(memDc);
    if (screenDc)
        ReleaseDC(nullptr, screenDc);
    return ok;
#else
    Q_UNUSED(out)
    Q_UNUSED(physDesktopOut)
    return false;
#endif
}

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
