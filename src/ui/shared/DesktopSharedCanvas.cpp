#include "DesktopSharedCanvas.h"

#include <cstring>

namespace poptools {

DesktopSharedCanvas::~DesktopSharedCanvas()
{
    dispose();
}

// 创建命名共享段并绑定双帧区 DIB：总大小 = 4096 头 + width×height×4×2（v4 双缓冲）；
// 失败路径逐级回滚（view→map→dibA）。段名固定（见 desktop_protocol.h）：与现存同名
// 段尺寸不兼容时创建失败——explorer 侧 DLL 长期持有旧段，见
// DesktopSurfaceHost::startDuty 的重试路径（T-062 BUG-02）
bool DesktopSharedCanvas::create(int width, int height)
{
    dispose();
    if (width <= 0 || height <= 0)
        return false;
    const SIZE_T frameSize = SIZE_T(width) * height * 4;
    const DWORD total = static_cast<DWORD>(kShmOffset + frameSize * 2);
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
    for (int i = 0; i < 2; ++i) {
        void *bits = nullptr;
        HBITMAP dib = ::CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, m_map,
                                         kShmOffset + frameSize * SIZE_T(i));
        if (!dib) {
            ::UnmapViewOfFile(m_view);
            m_view = nullptr;
            ::CloseHandle(m_map);
            m_map = nullptr;
            if (m_dibA) {
                ::DeleteObject(m_dibA);
                m_dibA = nullptr;
            }
            return false;
        }
        if (i == 0)
            m_dibA = dib;
        else
            m_dibB = dib;
    }
    // 位图数据直接落在共享段上；Format_ARGB32_Premultiplied 与 DIB 字节布局
    // 一致（BGRA 预乘 alpha），AlphaBlend AC_SRC_ALPHA 语义要求预乘。
    // m_image 绑帧区 0，仅作尺寸/格式参照（对外可见性由 frameIndex 决定）
    m_image = QImage(reinterpret_cast<uchar *>(frameBits(0)), width, height, width * 4,
                     QImage::Format_ARGB32_Premultiplied);
    m_width = width;
    m_height = height;
    writeHeaderInt(POPTOOLS_HDR_MAGIC, kMagic);
    writeHeaderInt(POPTOOLS_HDR_WIDTH, width);
    writeHeaderInt(POPTOOLS_HDR_HEIGHT, height);
    writeHeaderInt(POPTOOLS_HDR_READY, 0);
    writeHeaderInt(POPTOOLS_HDR_HOSTWND, 0);
    writeHeaderInt(POPTOOLS_HDR_FRAMEINDEX, 0);
    return true;
}

// 逆序释放：先让 QImage 脱离共享内存（防止悬垂引用），再删双 DIB、解除映射、关句柄
void DesktopSharedCanvas::dispose()
{
    m_image = QImage();
    if (m_dibA)
        ::DeleteObject(m_dibA);
    if (m_dibB)
        ::DeleteObject(m_dibB);
    m_dibA = nullptr;
    m_dibB = nullptr;
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

LONG DesktopSharedCanvas::readHeaderInt(int offset) const
{
    if (!m_view)
        return 0;
    return *reinterpret_cast<volatile const LONG *>(static_cast<const BYTE *>(m_view) + offset);
}

// 当前前台帧（explorer 正在采样的缓冲）浅拷贝；调用方只读不写
QImage DesktopSharedCanvas::frontImage() const
{
    if (!m_view || m_width <= 0 || m_height <= 0)
        return QImage();
    const int index = int(readHeaderInt(POPTOOLS_HDR_FRAMEINDEX)) & 1;
    return QImage(reinterpret_cast<const uchar *>(frameBits(index)), m_width, m_height,
                  m_width * 4, QImage::Format_ARGB32_Premultiplied);
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
    const LONG seq = readHeaderInt(POPTOOLS_HDR_SEQ);
    // 先 seq++ 后 ready=1 的写序：读端可凭「ready 置位时 seq 必为新值」判定帧完整；
    // 当前 DLL 读端只看 ready，seq 备将来版本/诊断用
    writeHeaderInt(POPTOOLS_HDR_SEQ, seq + 1);
    writeHeaderInt(POPTOOLS_HDR_READY, 1);
}

// v4 双缓冲原子发布：整帧写入后台帧区后原子翻转 frameIndex——explorer 擦除侧按
// 索引读前台帧，无论何时采样都只见到完整帧；整帧 memcpy 的中间态不可见
//（T-059 撕裂根治；dirty 参数保留签名兼容，实际整帧写后台，带宽 ~8MB/帧可接受）
void DesktopSharedCanvas::publishFrame(const QImage &frame, const QRect &dirty)
{
    Q_UNUSED(dirty)
    if (!m_view || m_image.isNull())
        return;
    if (frame.size() != m_image.size() || frame.format() != m_image.format())
        return;
    void *const back = frameBits(int(readHeaderInt(POPTOOLS_HDR_FRAMEINDEX) & 1) ^ 1);
    const int stride = m_image.bytesPerLine();
    if (frame.bytesPerLine() == stride) {
        memcpy(back, frame.constBits(), static_cast<size_t>(stride) * m_image.height());
    } else {
        for (int y = 0; y < m_image.height(); ++y)
            memcpy(static_cast<BYTE *>(back) + y * stride, frame.constScanLine(y), stride);
    }
    writeHeaderInt(POPTOOLS_HDR_FRAMEINDEX, int(readHeaderInt(POPTOOLS_HDR_FRAMEINDEX) & 1) ^ 1);
}

} // namespace poptools
