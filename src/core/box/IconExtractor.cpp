#include "IconExtractor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>      // 图像列表类型（ILD_TRANSPARENT/IMAGELISTDRAWPARAMS）
#include <commoncontrols.h> // IImageList / IID_IImageList（SHGetImageList 用）
#endif

namespace poptools {

IconExtractor &IconExtractor::instance()
{
    static IconExtractor inst;
    return inst;
}

// ---------- 应用图标 ----------

QPixmap IconExtractor::appIcon(const QString &target)
{
    const QString key = QDir::toNativeSeparators(target);
    auto it = m_appCache.find(key);
    if (it != m_appCache.end())
        return *it;

    QPixmap pixmap;
    if (QFile::exists(key))
        pixmap = extractShellIcon(key);
    if (pixmap.isNull())
        pixmap = placeholderIcon(QFileInfo(key).completeBaseName());
    m_appCache.insert(key, pixmap);
    return pixmap;
}

#ifdef Q_OS_WIN

namespace {

// HICON → QPixmap：GetIconInfo 拿到颜色位图后经 GetDIBits 读出 BGRA。
// 仅画 Dock 内部使用，不走 QIcon（规避 P-021）
QPixmap pixmapFromHIcon(HICON icon)
{
    ICONINFO info = {};
    if (!GetIconInfo(icon, &info))
        return QPixmap();

    QPixmap result;
    BITMAP bm = {};
    if (info.hbmColor && GetObject(info.hbmColor, sizeof(bm), &bm) && bm.bmWidth > 0) {
        const int w = bm.bmWidth, h = bm.bmHeight;
        BITMAPINFOHEADER bi = {};
        bi.biSize = sizeof(bi);
        bi.biWidth = w;
        bi.biHeight = -h; // 自顶向下
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;

        QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
        HDC hdc = GetDC(nullptr);
        GetDIBits(hdc, info.hbmColor, 0, h, img.bits(), reinterpret_cast<BITMAPINFO *>(&bi),
                  DIB_RGB_COLORS);
        ReleaseDC(nullptr, hdc);
        // 预乘 alpha：jumbo 图标的 alpha 通道为非预乘，QImage 需归一
        result = QPixmap::fromImage(img.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    } else if (info.hbmMask) {
        // 无颜色位图（单色图标）：用掩码生成黑色剪影，至少保证有内容
        BITMAP bmMask = {};
        if (GetObject(info.hbmMask, sizeof(bmMask), &bmMask) && bmMask.bmWidth > 0) {
            QImage img(bmMask.bmWidth, bmMask.bmHeight / 2, QImage::Format_ARGB32);
            img.fill(QColor(40, 44, 52));
            result = QPixmap::fromImage(img);
        }
    }
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    return result;
}

} // namespace

QPixmap IconExtractor::extractShellIcon(const QString &target)
{
    const QString path = QDir::toNativeSeparators(target);

    // jumbo 图标：SHGetFileInfoW 拿系统图像列表索引 → SHGetImageList(SHIL_JUMBO)
    // → IImageList::GetIcon。主线程 COM 由 QApplication(OleInitialize) 提供
    SHFILEINFOW sfi = {};
    const uintptr_t listIndex = SHGetFileInfoW(reinterpret_cast<const wchar_t *>(path.utf16()),
                                               0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX);
    if (listIndex) {
        IImageList *jumbo = nullptr;
        if (SUCCEEDED(SHGetImageList(SHIL_JUMBO, IID_IImageList, reinterpret_cast<void **>(&jumbo)))
            && jumbo) {
            HICON icon = nullptr;
            if (SUCCEEDED(jumbo->GetIcon(static_cast<int>(sfi.iIcon), ILD_TRANSPARENT, &icon))
                && icon) {
                QPixmap pixmap = pixmapFromHIcon(icon);
                DestroyIcon(icon);
                jumbo->Release();
                if (!pixmap.isNull())
                    return pixmap;
            } else {
                jumbo->Release();
            }
        }
    }

    // 回退：32px 标准提取（.lnk 默认跟随目标；部分冷门文件类型不在
    // jumbo 列表里）
    SHFILEINFOW sfi2 = {};
    if (SHGetFileInfoW(reinterpret_cast<const wchar_t *>(path.utf16()), 0, &sfi2, sizeof(sfi2),
                       SHGFI_ICON | SHGFI_LARGEICON)
        && sfi2.hIcon) {
        QPixmap pixmap = pixmapFromHIcon(sfi2.hIcon);
        DestroyIcon(sfi2.hIcon);
        return pixmap;
    }
    return QPixmap();
}

#else

QPixmap IconExtractor::extractShellIcon(const QString &target)
{
    Q_UNUSED(target);
    return QPixmap();
}

#endif

// 占位图：圆角方块 + 目标名首字符（UWP/冷门目标提不出图标时兜底）
QPixmap IconExtractor::placeholderIcon(const QString &name)
{
    QPixmap pixmap(256, 256);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(28, 28, 200, 200, 48, 48);
    p.fillPath(path, QColor(0x4C, 0x52, 0x60));
    p.setPen(QPen(QColor(255, 255, 255, 60), 4));
    p.drawPath(path);
    p.setPen(QColor(255, 255, 255, 220));
    QFont f = p.font();
    f.setPixelSize(110);
    f.setBold(true);
    p.setFont(f);
    p.drawText(pixmap.rect(), Qt::AlignCenter, name.left(1).toUpper());
    return pixmap;
}

// ---------- macOS 风格文件夹图标（自绘） ----------

QPixmap IconExtractor::folderIcon()
{
    if (!m_folderCache.isNull())
        return m_folderCache;

    QPixmap pixmap(256, 256);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);

    // 后板（带舌片的经典 macOS 后轮廓），深琥珀
    QPainterPath back;
    back.moveTo(48, 74);
    back.lineTo(48, 58);
    back.quadTo(48, 48, 58, 48);
    back.lineTo(96, 48);
    back.quadTo(106, 48, 112, 56);
    back.lineTo(120, 66);
    back.quadTo(124, 72, 132, 72);
    back.lineTo(204, 72);
    back.quadTo(214, 72, 214, 82);
    back.lineTo(214, 96);
    back.lineTo(48, 96);
    back.closeSubpath();
    p.fillPath(back, QColor(0xD9, 0x90, 0x2E));

    // 前板：亮黄纵渐变圆角面板
    QLinearGradient grad(0, 88, 0, 204);
    grad.setColorAt(0.0, QColor(0xFF, 0xE1, 0x9A));
    grad.setColorAt(1.0, QColor(0xF6, 0xBF, 0x55));
    QPainterPath front;
    front.addRoundedRect(QRectF(44, 88, 172, 118), 14, 14);
    p.fillPath(front, grad);
    // 前板顶部一条高光
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 70));
    p.drawRoundedRect(QRectF(48, 92, 164, 8), 4, 4);

    m_folderCache = pixmap;
    return pixmap;
}

} // namespace poptools
