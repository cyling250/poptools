#include "DesktopSharedCanvas.h"

#include <cstring>

namespace poptools {

DesktopSharedCanvas::~DesktopSharedCanvas()
{
    dispose();
}

// 创建命名共享段并绑定 DIB：总大小 = 4096 头 + width×height×4；失败路径逐级回滚
//（view→map）。段名固定（见 desktop_protocol.h）：与现存同名段尺寸不兼容时
// 创建失败——explorer 侧 DLL 长期持有旧段，见 DesktopSurfaceHost::startDuty 的
// 重试路径（T-062 BUG-02）
bool DesktopSharedCanvas::create(int width, int height)
{
    dispose();
    if (width <= 0 || height <= 0)
        return false;
    const DWORD total = static_cast<DWORD>(kShmOffset) + static_cast<DWORD>(width) * height * 4;
    m_map = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, total,
                                 POPTOOLS_DESKTOP_SHM_NAME);
    if (!m_map)
        return false;
    m_view = ::MapViewOfFile(m_map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!m_view) {
        ::CloseHandle(m_map);
        m_map = nullptr;
        return false;
    }
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height; // 顶行在前（与 QImage 一致）
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    m_dib = ::CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &m_bits, m_map, kShmOffset);
    if (!m_dib) {
        ::UnmapViewOfFile(m_view);
        m_view = nullptr;
        ::CloseHandle(m_map);
        m_map = nullptr;
        return false;
    }
    // 位图数据直接落在共享段上；Format_ARGB32_Premultiplied 与 DIB 字节布局
    // 一致（BGRA 预乘 alpha），AlphaBlend AC_SRC_ALPHA 语义要求预乘
    m_image = QImage(reinterpret_cast<uchar *>(m_bits), width, height, width * 4,
                     QImage::Format_ARGB32_Premultiplied);
    m_width = width;
    m_height = height;
    writeHeaderInt(POPTOOLS_HDR_MAGIC, kMagic);
    writeHeaderInt(POPTOOLS_HDR_WIDTH, width);
    writeHeaderInt(POPTOOLS_HDR_HEIGHT, height);
    writeHeaderInt(POPTOOLS_HDR_READY, 0);
    writeHeaderInt(POPTOOLS_HDR_HOSTWND, 0);
    return true;
}

// 逆序释放：先让 QImage 脱离 m_bits（防止悬垂引用），再删 DIB、解除映射、关句柄
void DesktopSharedCanvas::dispose()
{
    m_image = QImage();
    if (m_dib)
        ::DeleteObject(m_dib);
    m_dib = nullptr;
    m_bits = nullptr;
    if (m_view)
        ::UnmapViewOfFile(m_view);
    m_view = nullptr;
    if (m_map)
        ::CloseHandle(m_map);
    m_map = nullptr;
    m_width = 0;
    m_height = 0;
}

// 向共享头指定偏移写一个 LONG（volatile 直写；应用侧仅 UI 单写者，无需 Interlocked）
void DesktopSharedCanvas::writeHeaderInt(int offset, LONG value)
{
    if (!m_view)
        return;
    *reinterpret_cast<volatile LONG *>(static_cast<BYTE *>(m_view) + offset) = value;
}

// 宿主 HWND 写入头部，供 DLL ForwardEvent 投递鼠标事件；HWND 按 USER 句柄惯例
// 截断 32 位（DLL 侧 (HWND)(INT_PTR) 符号扩展还原，微软官方惯例）
void DesktopSharedCanvas::setHostWindow(HWND hwnd)
{
    writeHeaderInt(POPTOOLS_HDR_HOSTWND, static_cast<LONG>(reinterpret_cast<LONG_PTR>(hwnd)));
}

void DesktopSharedCanvas::setSlotRect(int slot, const QRect &r)
{
    if (slot < 0 || slot >= kMaxSlots)
        return;
    const int base = POPTOOLS_HDR_SLOTRECT0 + slot * 16;
    writeHeaderInt(base + 0, r.left());
    writeHeaderInt(base + 4, r.top());
    writeHeaderInt(base + 8, r.left() + r.width()); // C# Rectangle.Right = X+Width
    writeHeaderInt(base + 12, r.top() + r.height());
}

void DesktopSharedCanvas::markReady()
{
    if (!m_view)
        return;
    const LONG seq =
        *reinterpret_cast<volatile LONG *>(static_cast<BYTE *>(m_view) + POPTOOLS_HDR_SEQ);
    // 先 seq++ 后 ready=1 的写序：读端可凭「ready 置位时 seq 必为新值」判定帧完整；
    // 当前 DLL 读端只看 ready，seq 备将来版本/诊断用
    writeHeaderInt(POPTOOLS_HDR_SEQ, seq + 1);
    writeHeaderInt(POPTOOLS_HDR_READY, 1);
}

void DesktopSharedCanvas::publishFrame(const QImage &frame, const QRect &dirty)
{
    if (!m_dib || m_image.isNull())
        return;
    if (frame.size() != m_image.size() || frame.format() != m_image.format())
        return;
    const int stride = m_image.bytesPerLine();
    const QRect r = dirty.isEmpty() ? m_image.rect() : dirty.intersected(m_image.rect());
    if (r.isEmpty())
        return;
    // 全幅且源步距一致 → 单次 memcpy（最快路径）；否则逐行拷，容忍源帧 bytesPerLine 不同
    if (r == m_image.rect() && frame.bytesPerLine() == stride) {
        memcpy(m_bits, frame.constBits(), static_cast<size_t>(stride) * m_image.height());
        return;
    }
    const size_t xBytes = static_cast<size_t>(r.left()) * 4;
    const size_t wBytes = static_cast<size_t>(r.width()) * 4;
    for (int y = r.top(); y <= r.bottom(); ++y)
        memcpy(static_cast<BYTE *>(m_bits) + y * stride + xBytes,
               frame.constScanLine(y) + xBytes, wBytes);
}

} // namespace poptools
