#include "IconService.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>       // IMAGELISTDRAWPARAMS/IMAGEINFO（commoncontrols 前置）
#include <commoncontrols.h> // IImageList / IID_IImageList（SHIL_JUMBO 系统图像列表）
#include <shellapi.h>
#include <shlobj.h>

#include <QDir>
#include <QThreadPool>

#include <memory>

namespace poptools {

namespace {

// COM 初始化 RAII（线程池线程每次自带，避免依赖全局状态）
struct ComGuard
{
    bool ok = false;
    explicit ComGuard(DWORD model) { ok = SUCCEEDED(CoInitializeEx(nullptr, model)); }
    ~ComGuard()
    {
        if (ok)
            CoUninitialize();
    }
};

// COM 智能释放
struct ComRelease
{
    void operator()(IUnknown *p) const
    {
        if (p)
            p->Release();
    }
};

// HICON → 32bpp 预乘 QImage（P2-1 教训：不预乘半透明发黑）
QImage hiconToImage(HICON icon)
{
    if (!icon)
        return QImage();
    ICONINFO ii = {};
    if (!GetIconInfo(icon, &ii))
        return QImage();
    QImage out;
    do {
        if (!ii.hbmColor)
            break;
        BITMAP bm = {};
        if (!GetObjectW(ii.hbmColor, sizeof(bm), &bm) || bm.bmBitsPixel != 32)
            break;
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = bm.bmWidth;
        bi.bmiHeader.biHeight = -bm.bmHeight; // 顶行在前
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        QImage img(bm.bmWidth, bm.bmHeight, QImage::Format_ARGB32);
        if (!GetDIBits(GetDC(nullptr), ii.hbmColor, 0, bm.bmHeight, img.bits(), &bi,
                       DIB_RGB_COLORS)) {
            break;
        }
        // 老图标可能整图 alpha=0（透明通道缺失）：按不透明处理，防全隐身
        int maxAlpha = 0;
        for (int y = 0; y < img.height() && maxAlpha == 0; ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                if (qAlpha(line[x]) > 0) {
                    maxAlpha = 255;
                    break;
                }
            }
        }
        if (maxAlpha == 0) {
            for (int y = 0; y < img.height(); ++y) {
                QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
                for (int x = 0; x < img.width(); ++x)
                    line[x] |= 0xFF000000;
            }
        }
        out = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    } while (false);
    if (ii.hbmColor)
        DeleteObject(ii.hbmColor);
    if (ii.hbmMask)
        DeleteObject(ii.hbmMask);
    DestroyIcon(icon);
    return out;
}

} // namespace

IconService::IconService(QObject *parent)
    : QObject(parent)
{
}

QImage IconService::cachedIcon(const QString &target) const
{
    return m_cache.value(target);
}

void IconService::requestIcon(const QString &target,
                              std::function<void(const QString &, const QImage &)> cb)
{
    if (target.isEmpty())
        return;
    const auto it = m_cache.constFind(target);
    if (it != m_cache.constEnd()) {
        // 命中（含负缓存：失败空图）——不再请求，防止失败风暴饿死主线程
        if (cb)
            cb(target, it.value());
        return;
    }
    if (m_inFlight.contains(target))
        return;
    m_inFlight.insert(target);
    // 红线 1：SHGetFileInfo/IImageList 是 shell 调用，禁入主线程——线程池提取，
    // 结果经 invokeMethod 回主线程入缓存并通知渲染（接收者销毁则事件丢弃）
    IconService *svc = this;
    QThreadPool::globalInstance()->start([svc, target, cb] {
        const QImage img = extractIcon(target);
        QMetaObject::invokeMethod(
            svc,
            [svc, target, img, cb] {
                svc->m_inFlight.remove(target);
                // 失败也入负缓存（空图）：该 target 永不二次提取
                svc->m_cache.insert(target, img);
                if (cb)
                    cb(target, img);
            },
            Qt::QueuedConnection);
    });
}

QImage IconService::extractIcon(const QString &target)
{
    ComGuard com(COINIT_APARTMENTTHREADED);
    if (!com.ok)
        return QImage();
    const std::wstring path = QDir::toNativeSeparators(target).toStdWString();

    // jumbo 路线：系统图标索引 → 256px 系统图像列表 → 透明图标句柄
    SHFILEINFOW sfi = {};
    const DWORD_PTR idx = SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                                         SHGFI_SYSICONINDEX);
    if (idx) {
        IImageList *jumbo = nullptr;
        if (SUCCEEDED(SHGetImageList(SHIL_JUMBO, IID_IImageList,
                                     reinterpret_cast<void **>(&jumbo)))
            && jumbo) {
            std::unique_ptr<IImageList, ComRelease> jumboGuard(jumbo);
            HICON h = nullptr;
            if (SUCCEEDED(jumbo->GetIcon(static_cast<int>(idx), ILD_TRANSPARENT, &h)) && h)
                return hiconToImage(h);
        }
    }
    // 回退：32px 大图标（.lnk 经 SHGFI_ICON 自动跟随目标图标）
    if (SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_ICON)
        && sfi.hIcon) {
        return hiconToImage(sfi.hIcon);
    }
    return QImage();
}

} // namespace poptools
