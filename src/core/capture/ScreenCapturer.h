#pragma once

#include <QImage>
#include <QPixmap>
#include <QRect>
#include <QPoint>

class QScreen;

namespace poptools {

// 一次截屏的结果：物理分辨率画面 + 逻辑/物理虚拟桌面几何
struct CaptureResult
{
    QPixmap screen;      // 虚拟桌面画面（物理像素，dpr=1）
    QRect desktop;       // 虚拟桌面逻辑几何（窗口/鼠标/选区均用逻辑坐标）
    QRect physDesktop;   // 虚拟桌面物理几何（screen 坐标系）
};

// ============================================================
// 屏幕捕获与 DPI 坐标映射（纯工具类，不依赖具体窗口）
//
// 背景：Qt 全屏截图存在两个坐标系——
//   逻辑坐标（窗口/鼠标/选区）与物理像素（grabWindow 原图）。
// grabWindow 返回物理像素图，若按逻辑尺寸创建画布会被 drawPixmap
// 压缩，导致截图掉分辨率。因此按"屏幕逻辑原点×dpr + 屏幕内偏移×dpr"
// 统一换算，输出时必须在此边界转换。
// ============================================================
class ScreenCapturer
{
public:
    // 拼接所有屏幕为一张物理分辨率画面（多显示器扩展屏无黑屏）
    static CaptureResult capture();

    // 线程池可用的纯 GDI 虚拟桌面截屏（BitBlt 直出 QImage，不经任何 Qt GUI
    // 对象，任意线程可调）。成功时 *out 为 ARGB32 物理整图、*physDesktopOut
    // 为虚拟桌面物理几何。T-068：监控 OCR 兜底截屏挪线程池用（主线程挂
    // 低级钩子，大桌面 BitBlt 数十 ms 即全系统鼠标排队）
    static bool captureVirtualDesktop(QImage *out, QRect *physDesktopOut);

    // 屏幕逻辑几何 → 物理像素几何（Qt 6.8 的 QScreen 无 nativeGeometry()，
    // 按每显示器 DPI 映射；QRect 右/下边界含端点，换算用 (right+1, bottom+1) 避免丢像素）
    static QRect screenPhysicalRect(const QScreen *s);

    // 逻辑全局点 → 物理像素点（按点所在屏幕 dpr 换算，回退主屏）
    static QPoint toPhysical(const QPoint &logicalGlobal);

    // 逻辑全局矩形 → 物理像素矩形（选区输出用）
    static QRect toPhysicalRect(const QRect &logicalGlobal);
};

} // namespace poptools
