#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "../../hook/desktop_protocol.h" // 跨进程协议常量唯一事实源（S-01）

#include <QImage>
#include <QRect>

namespace poptools {

// ============================================================
// 桌面共享画布（T-054 照搬 rlqd CalendarClone/SharedCanvas.cs；T-059 v2
// 双面板；T-060 v3 槽位数组——任意贴壁纸面板占一槽，0=日历 1..N=收纳盒）。
// CreateFileMapping(命名) + CreateDIBSection(绑定共享段) —— 应用侧经
// QImage 绘制，explorer 内的注入 DLL 直接 AlphaBlend 读它。
// 头部布局：magic/width/height/ready/hostWnd/slotRect[SLOTS]/seq
//（头保留区 + 位图数据）；槽矩形供 DLL 计算脏矩形失效范围，空槽跳过。
// 段名/魔数/槽位数/头偏移等协议值一律见 desktop_protocol.h（唯一事实源，
// 此处不手写数值以免与协议头漂移）；跨版本结构不兼容，失配即拒画。
// ============================================================
class DesktopSharedCanvas
{
public:
    // 协议常量别名（值定义见 desktop_protocol.h，应用侧与 DLL 同源）
    static constexpr int kShmOffset = POPTOOLS_DESKTOP_SHM_OFFSET;
    static constexpr int kMagic = POPTOOLS_DESKTOP_SHM_MAGIC; // CLK3（v3）
    static constexpr int kMaxSlots = POPTOOLS_DESKTOP_SHM_SLOTS;

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
    // 原子发布（v4 双缓冲帧翻转）：整帧写入后台帧区后原子翻转 frameIndex——
    // explorer 擦除侧按索引读前台帧，任何时刻采到的都是完整帧，memcpy 中间态
    // 不可见（T-059 撕裂根治；dirty 参数仅保留签名兼容，实际整帧写后台）
    void publishFrame(const QImage &frame, const QRect &dirty = QRect());
    // 当前前台帧（explorer 正在采样的缓冲），浅拷贝引用计数
    QImage frontImage() const;

private:
    void writeHeaderInt(int offsetInHeader, LONG value);
    LONG readHeaderInt(int offsetInHeader) const;
    void *headerPtr() { return m_view; }
    void *frameBits(int index) const
    {
        return static_cast<BYTE *>(m_view) + kShmOffset
             + SIZE_T(m_width) * m_height * 4 * SIZE_T(index);
    }

    HANDLE m_map = nullptr;
    void *m_view = nullptr;
    HBITMAP m_dibA = nullptr;
    HBITMAP m_dibB = nullptr;
    QImage m_image;
    int m_width = 0;
    int m_height = 0;
};

} // namespace poptools
