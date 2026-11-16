#include "DesktopSharedCanvas.h"

#include <cstring>

namespace poptools {

namespace {
constexpr wchar_t kShmName[] = L"Local\\PopToolsDesktopShared_v3";
// 头部字段偏移（与注入 DLL 的 SharedHeader 逐字段对齐）
constexpr int kOffMagic = 0;
constexpr int kOffWidth = 4;
constexpr int kOffHeight = 8;
constexpr int kOffReady = 12;
constexpr int kOffHostWnd = 16;
constexpr int kOffSlotRect0 = 20; // slotRect[6][4]，每槽 16 字节 → 20..115
constexpr int kOffSeq = 20 + DesktopSharedCanvas::kMaxSlots * 16;
} // namespace

DesktopSharedCanvas::~DesktopSharedCanvas()
{
    dispose();
}

bool DesktopSharedCanvas::create(int width, int height)
{
    dispose();
    if (width <= 0 || height <= 0)
        return false;
    const DWORD total = static_cast<DWORD>(kShmOffset) + static_cast<DWORD>(width) * height * 4;
    m_map = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, total, kShmName);
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
    writeHeaderInt(kOffMagic, kMagic);
    writeHeaderInt(kOffWidth, width);
    writeHeaderInt(kOffHeight, height);
    writeHeaderInt(kOffReady, 0);
    writeHeaderInt(kOffHostWnd, 0);
    return true;
}

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

void DesktopSharedCanvas::writeHeaderInt(int offset, LONG value)
{
    if (!m_view)
        return;
    *reinterpret_cast<volatile LONG *>(static_cast<BYTE *>(m_view) + offset) = value;
}

void DesktopSharedCanvas::setHostWindow(HWND hwnd)
{
    writeHeaderInt(kOffHostWnd, static_cast<LONG>(reinterpret_cast<LONG_PTR>(hwnd)));
}

void DesktopSharedCanvas::setSlotRect(int slot, const QRect &r)
{
    if (slot < 0 || slot >= kMaxSlots)
        return;
    const int base = kOffSlotRect0 + slot * 16;
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
        *reinterpret_cast<volatile LONG *>(static_cast<BYTE *>(m_view) + kOffSeq);
    writeHeaderInt(kOffSeq, seq + 1);
    writeHeaderInt(kOffReady, 1);
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
