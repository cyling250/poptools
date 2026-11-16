#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <QImage>
#include <QRect>

namespace poptools {

// ============================================================
// 桌面共享画布（T-054 照搬 rlqd CalendarClone/SharedCanvas.cs；T-059 v2
// 双面板；T-060 v3 槽位数组——任意贴壁纸面板占一槽，0=日历 1..N=收纳盒）。
// CreateFileMapping(命名) + CreateDIBSection(绑定共享段) —— 应用侧经
// QImage 绘制，explorer 内的注入 DLL 直接 AlphaBlend 读它。
// 头部布局：magic/width/height/ready/hostWnd/slotRect[6]/seq
//（4096 头 + 位图数据）；槽矩形供 DLL 计算脏矩形失效范围，空槽跳过。
// 段名与 magic 随版本升级（跨版本结构不兼容，失配即拒画）。
// ============================================================
class DesktopSharedCanvas
{
public:
    static constexpr int kShmOffset = 4096;
    static constexpr int kMagic = 0x434C4B33; // CLK3（v3）
    static constexpr int kMaxSlots = 6;       // 与 DLL SHM_SLOTS 一致

    DesktopSharedCanvas() = default;
    ~DesktopSharedCanvas();
    Q_DISABLE_COPY(DesktopSharedCanvas)

    bool create(int width, int height);
    void dispose();
    bool isValid() const { return !m_image.isNull(); }

    int width() const { return m_width; }
    int height() const { return m_height; }
    QImage &image() { return m_image; }

    void setHostWindow(HWND hwnd);
    // 面板槽矩形（slot ∈ [0,kMaxSlots)；DLL 据此算脏矩形，空矩形跳过）
    void setSlotRect(int slot, const QRect &r);
    void markReady(); // seq++ + ready=1（DLL 端据此决定是否上屏）
    // 原子发布：把绘制好的暂存帧拷入共享位图；dirty 非空时只拷该矩形
    //（悬停这类高频小改动免整屏 memcpy）。共享位图是 explorer 随时可采样的，
    // 不能在上面"清屏→重绘"——中间态被采到就是整面板消失一帧
    void publishFrame(const QImage &frame, const QRect &dirty = QRect());

private:
    void writeHeaderInt(int offsetInHeader, LONG value);
    void *headerPtr() { return m_view; }

    HANDLE m_map = nullptr;
    void *m_view = nullptr;
    HBITMAP m_dib = nullptr;
    void *m_bits = nullptr;
    QImage m_image;
    int m_width = 0;
    int m_height = 0;
};

} // namespace poptools
