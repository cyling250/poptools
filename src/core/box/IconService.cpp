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

#include <QCoreApplication>
#include <QDir>
#include <QPointer>
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
        // GetDIBits 需要一个 DC 仅为调色板参数：用屏幕 DC 且用完即 ReleaseDC
        //（T-062 修复: 原实现 GetDC 返回值未保存未释放，每枚图标漏一个 GDI 句柄，BUG-07）
        HDC dc = GetDC(nullptr);
        const bool got = dc && GetDIBits(dc, ii.hbmColor, 0, bm.bmHeight, img.bits(), &bi,
                                         DIB_RGB_COLORS);
        if (dc)
            ReleaseDC(nullptr, dc);
        if (!got)
            break;
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

// 空构造；m_cache/m_inFlight 仅主线程访问（worker 只回传不入表），随 parent 析构
IconService::IconService(QObject *parent)
    : QObject(parent)
{
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
    // 结果经 invokeMethod 回主线程入缓存并通知渲染。
    // T-062 修复: worker 原来按值捕获裸 this（BUG-01）：提取是 shell 调用，耗时
    // 数十至数百 ms，期间面板可能被 deleteLater（管理器增删改触发全面板重建），
    // 之后 invokeMethod(svc) 以悬空指针为接收者是 UB。改为 QPointer 守卫 + 上下文
    // 挂 qApp：结果必达主线程队列，落地时服务已亡则整包丢弃（qApp 生命周期覆盖
    // 一切，队列事件在其析构后自然丢弃）。线程模型不变
    QCoreApplication *app = QCoreApplication::instance();
    QPointer<IconService> guard(this);
    QThreadPool::globalInstance()->start([app, guard, target, cb] {
        const QImage img = extractIcon(target);
        if (!app)
            return; // 应用已退出，无处投递
        QMetaObject::invokeMethod(
            app,
            [guard, target, img, cb] {
                if (!guard)
                    return; // 服务已亡（含其宿主面板）：负缓存随对象同亡，整包丢弃
                guard->m_inFlight.remove(target);
                // 失败也入负缓存（空图）：该 target 永不二次提取
                guard->m_cache.insert(target, img);
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
    // 返回值即系统图标索引；0 兼具"失败/索引0"歧义（索引0=未知类型图标），
    // 命中 0 会回落 32px 路径——视觉无缺陷仅分辨率降档，可接受（BUG-10 登记，本批不动）
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
