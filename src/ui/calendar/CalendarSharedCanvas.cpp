#include "CalendarSharedCanvas.h"

namespace poptools {

namespace {
constexpr wchar_t kShmName[] = L"Local\\PopToolsCalendarShared_v1";
// 头部字段偏移（与注入 DLL 的 SharedHeader 逐字段对齐）
constexpr int kOffMagic = 0;
constexpr int kOffWidth = 4;
constexpr int kOffHeight = 8;
constexpr int kOffReady = 12;
constexpr int kOffHostWnd = 16;
constexpr int kOffRectL = 20;
constexpr int kOffRectT = 24;
constexpr int kOffRectR = 28;
constexpr int kOffRectB = 32;
constexpr int kOffSeq = 36;
} // namespace

CalendarSharedCanvas::~CalendarSharedCanvas()
{
    dispose();
}

bool CalendarSharedCanvas::create(int width, int height)
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

void CalendarSharedCanvas::dispose()
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

void CalendarSharedCanvas::writeHeaderInt(int offset, LONG value)
{
    if (!m_view)
        return;
    *reinterpret_cast<volatile LONG *>(static_cast<BYTE *>(m_view) + offset) = value;
}

void CalendarSharedCanvas::setHostWindow(HWND hwnd)
{
    writeHeaderInt(kOffHostWnd, static_cast<LONG>(reinterpret_cast<LONG_PTR>(hwnd)));
}

void CalendarSharedCanvas::setCalendarRect(const QRect &r)
{
    writeHeaderInt(kOffRectL, r.left());
    writeHeaderInt(kOffRectT, r.top());
    writeHeaderInt(kOffRectR, r.left() + r.width()); // C# Rectangle.Right = X+Width
    writeHeaderInt(kOffRectB, r.top() + r.height());
}

void CalendarSharedCanvas::markReady()
{
    if (!m_view)
        return;
    const LONG seq =
        *reinterpret_cast<volatile LONG *>(static_cast<BYTE *>(m_view) + kOffSeq);
    writeHeaderInt(kOffSeq, seq + 1);
    writeHeaderInt(kOffReady, 1);
}

} // namespace poptools
