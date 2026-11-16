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
// 共享内存画布（照搬 rlqd CalendarClone/SharedCanvas.cs）：
// CreateFileMapping(命名) + CreateDIBSection(绑定共享段) —— 应用侧经
// QImage 直接绘制进共享位图，explorer 内的注入 DLL 直接 AlphaBlend 读它。
// 头部布局：magic/width/height/ready/hostWnd/rect/seq（4096 头 + 位图数据）。
// ============================================================
class CalendarSharedCanvas
{
public:
    static constexpr int kShmOffset = 4096;
    static constexpr int kMagic = 0x434C4B31; // CLK1

    CalendarSharedCanvas() = default;
    ~CalendarSharedCanvas();
    Q_DISABLE_COPY(CalendarSharedCanvas)

    bool create(int width, int height);
    void dispose();
    bool isValid() const { return !m_image.isNull(); }

    int width() const { return m_width; }
    int height() const { return m_height; }
    QImage &image() { return m_image; }

    void setHostWindow(HWND hwnd);
    void setCalendarRect(const QRect &r);
    void markReady(); // seq++ + ready=1（DLL 端据此决定是否上屏）
    // 整帧原子发布：把绘制好的暂存帧一次拷入共享位图。共享位图是 explorer
    // 随时可采样的，不能在上面"清屏→重绘"——中间态被采到就是整面板消失一帧
    void publishFrame(const QImage &frame);

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
