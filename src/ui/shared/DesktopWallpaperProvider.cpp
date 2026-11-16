#include "DesktopWallpaperProvider.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QLinearGradient>
#include <QPainter>
#include <QSettings>

#include <algorithm>

namespace poptools {

// 记录毛玻璃输出目标尺寸（与画布同尺寸）
DesktopWallpaperProvider::DesktopWallpaperProvider(int width, int height)
    : m_w(width)
    , m_h(height)
{
}

QImage DesktopWallpaperProvider::blurredWallpaper()
{
    if (!m_blurred.isNull() && m_lastRefresh.msecsTo(QDateTime::currentDateTimeUtc()) < 20000)
        return m_blurred;
    refresh();
    return m_blurred;
}

// 强制刷新（绕过 20s 缓存）：磁盘读图 + 1/4 降采样 + 两遍模糊 + 放大，全程调用
// 线程同步执行——宿主 30s 定时器触发，勿挪进交互关键路径
void DesktopWallpaperProvider::refresh()
{
    const QString path = wallpaperPath();
    QImage src;
    if (!path.isEmpty() && QFile::exists(path)) {
        src = QImage(path);
        if (src.format() == QImage::Format_Invalid)
            src = QImage();
    }
    QImage composed = renderWallpaper(src);
    const int sw = std::max(1, m_w / 4);
    const int sh = std::max(1, m_h / 4);
    QImage small = composed.scaled(sw, sh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    composed = QImage();
    QImage blurred = boxBlur(small.convertToFormat(QImage::Format_ARGB32), 10, 2);
    m_blurred = blurred.scaled(m_w, m_h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                    .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_lastRefresh = QDateTime::currentDateTimeUtc();
    m_lastPath = path;
}

// 壁纸路径探测：注册表 WallPaper / TranscodedWallpaper / CachedFiles 最新一张 /
// 聚焦候选（>200KB 且 ≥1024×600，整图解码成功才收），全部存在者按修改时间取最新。
// 曾按 S-07 试 QImageReader::size() 只读文件头免整图解码——但"头完整、扫描数据
// 截断"的图 size() 仍返回有效尺寸，不实际解码就无法识别，候选语义与基线出现
// 行为分歧（收进坏候选 vs 基线取次新有效候选）；严格等价做不到，按行为等价红线
// 回退为整图解码（T-062 R3 G-01 方案 A）
QString DesktopWallpaperProvider::wallpaperPath() const
{
    struct Candidate
    {
        QDateTime time;
        QString path;
    };
    QVector<Candidate> candidates;
    auto add = [&candidates](const QString &raw) {
        if (raw.trimmed().isEmpty())
            return;
        const QString path = QDir::fromNativeSeparators(
            QString(raw).replace(QLatin1String("%SystemRoot%"), QStringLiteral("C:/Windows")));
        if (QFile::exists(path))
            candidates.append({ QFileInfo(path).lastModified(), path });
    };

    // 注册表壁纸（HKCU\Control Panel\Desktop\WallPaper）
    const QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Control Panel\\Desktop"),
                        QSettings::NativeFormat);
    add(reg.value(QStringLiteral("WallPaper")).toString());

    // 活动壁纸缓存
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty()) {
        const QString trans = QDir(appData + QStringLiteral("/Microsoft/Windows/Themes"))
                                  .absoluteFilePath(QStringLiteral("TranscodedWallpaper"));
        add(trans);
        const QDir cachedDir(appData + QStringLiteral("/Microsoft/Windows/Themes/CachedFiles"));
        if (cachedDir.exists()) {
            const QFileInfoList files =
                cachedDir.entryInfoList(QDir::Files, QDir::Time);
            if (!files.isEmpty())
                add(files.first().absoluteFilePath());
        }
    }

    // Windows 聚焦壁纸（开启且存在大于 200KB、≥1024x600 的候选时取最新一张）
    bool spotlight = false;
    const QSettings cdm(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion"
                       "\\ContentDeliveryManager"),
        QSettings::NativeFormat);
    spotlight = cdm.value(QStringLiteral("SubscribedContent-338389Enabled"), 0).toInt() == 1;
    if (spotlight) {
        const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
        const QDir assetsDir(localAppData
                             + QStringLiteral("/Packages/Microsoft.Windows."
                                              "ContentDeliveryManager_cw5n1h2txyewy/"
                                              "LocalState/Assets"));
        if (assetsDir.exists()) {
            const QFileInfoList files =
                assetsDir.entryInfoList(QDir::Files, QDir::Time);
            for (const QFileInfo &fi : files) {
                if (fi.size() <= 200000)
                    continue;
                const QImage img(fi.absoluteFilePath());
                if (!img.isNull() && img.width() >= 1024 && img.height() >= 600) {
                    add(fi.absoluteFilePath());
                    break;
                }
            }
        }
    }

    if (candidates.isEmpty())
        return {};
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate &a, const Candidate &b) {
                         return a.time > b.time;
                     });
    return candidates.first().path;
}

// 按系统 WallpaperStyle 铺排：tile+style0=平铺、2=拉伸、10=填充(max 比例)、
// 其余=适应(min 比例)；无图时暗色渐变兜底。style0（系统居中=1:1 原尺寸）按
// 适应缩放处理与真实壁纸形变有偏差——登记 BUG-09（保真修复需改视觉输出，
// 超出行为等价范畴，本轮不修）
QImage DesktopWallpaperProvider::renderWallpaper(const QImage &src) const
{
    QImage bmp(m_w, m_h, QImage::Format_ARGB32);
    bmp.fill(QColor(10, 12, 20));
    QPainter g(&bmp);
    g.setRenderHint(QPainter::SmoothPixmapTransform);
    if (src.isNull()) {
        QLinearGradient grad(0, 0, m_w, m_h);
        grad.setCoordinateMode(QGradient::ObjectMode);
        grad.setColorAt(0.0, QColor(24, 28, 52));
        grad.setColorAt(1.0, QColor(8, 10, 20));
        g.fillRect(0, 0, m_w, m_h, grad);
        return bmp;
    }
    int style = 10;
    bool tile = false;
    const QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Control Panel\\Desktop"),
                        QSettings::NativeFormat);
    bool ok = false;
    const int sv = reg.value(QStringLiteral("WallpaperStyle")).toString().toInt(&ok);
    if (ok)
        style = sv;
    tile = reg.value(QStringLiteral("TileWallpaper")).toString() == QStringLiteral("1");

    if (tile && style == 0) {
        g.drawTiledPixmap(0, 0, m_w, m_h, QPixmap::fromImage(src));
        return bmp;
    }
    QRect dst;
    if (style == 2) {
        dst = QRect(0, 0, m_w, m_h); // 拉伸
    } else if (style == 10) {
        const double scale = std::max(double(m_w) / src.width(), double(m_h) / src.height()); // 填充
        dst = QRect((m_w - int(src.width() * scale)) / 2, (m_h - int(src.height() * scale)) / 2,
                    std::max(1, int(src.width() * scale)), std::max(1, int(src.height() * scale)));
    } else {
        const double scale = std::min(double(m_w) / src.width(), double(m_h) / src.height()); // 适应/居中
        dst = QRect((m_w - int(src.width() * scale)) / 2, (m_h - int(src.height() * scale)) / 2,
                    std::max(1, int(src.width() * scale)), std::max(1, int(src.height() * scale)));
    }
    g.drawImage(dst, src);
    return bmp;
}

QImage DesktopWallpaperProvider::boxBlur(const QImage &src, int radius, int passes)
{
    // 与 rlqd 逐字节同构：ARGB32 字节序 BGRA，滑窗累加水平 + 垂直各一遍
    QImage bmp = src.convertToFormat(QImage::Format_ARGB32);
    const int w = bmp.width(), h = bmp.height(), stride = bmp.bytesPerLine();
    QVector<uchar> buf(stride * h);
    QVector<uchar> tmp(stride * h);
    memcpy(buf.data(), bmp.constBits(), size_t(buf.size()));
    for (int p = 0; p < passes; ++p) {
        boxBlurH(buf.constData(), tmp.data(), w, h, stride, radius);
        boxBlurV(tmp.constData(), buf.data(), w, h, stride, radius);
    }
    memcpy(bmp.bits(), buf.constData(), size_t(buf.size()));
    return bmp;
}

// 水平滑窗盒模糊：进出像素增量更新均摊 O(1)；边缘 clamp 等效扩边重复计权
void DesktopWallpaperProvider::boxBlurH(const uchar *src, uchar *dst, int w, int h, int stride, int r)
{
    const int div = r * 2 + 1;
    for (int y = 0; y < h; ++y) {
        const int row = y * stride;
        int sb = 0, sg = 0, sr = 0, sa = 0;
        for (int x = -r; x <= r; ++x) {
            const int xx = std::clamp(x, 0, w - 1);
            const int i = row + xx * 4;
            sb += src[i];
            sg += src[i + 1];
            sr += src[i + 2];
            sa += src[i + 3];
        }
        for (int x = 0; x < w; ++x) {
            const int o = row + x * 4;
            dst[o] = uchar(sb / div);
            dst[o + 1] = uchar(sg / div);
            dst[o + 2] = uchar(sr / div);
            dst[o + 3] = uchar(sa / div);
            const int addIdx = std::clamp(x + r + 1, 0, w - 1);
            const int remIdx = std::clamp(x - r, 0, w - 1);
            const int ai = row + addIdx * 4, ri = row + remIdx * 4;
            sb += src[ai] - src[ri];
            sg += src[ai + 1] - src[ri + 1];
            sr += src[ai + 2] - src[ri + 2];
            sa += src[ai + 3] - src[ri + 3];
        }
    }
}

// 垂直滑窗盒模糊（与 boxBlurH 同构，按列走）
void DesktopWallpaperProvider::boxBlurV(const uchar *src, uchar *dst, int w, int h, int stride, int r)
{
    const int div = r * 2 + 1;
    for (int x = 0; x < w; ++x) {
        const int col = x * 4;
        int sb = 0, sg = 0, sr = 0, sa = 0;
        for (int y = -r; y <= r; ++y) {
            const int yy = std::clamp(y, 0, h - 1);
            const int i = yy * stride + col;
            sb += src[i];
            sg += src[i + 1];
            sr += src[i + 2];
            sa += src[i + 3];
        }
        for (int y = 0; y < h; ++y) {
            const int o = y * stride + col;
            dst[o] = uchar(sb / div);
            dst[o + 1] = uchar(sg / div);
            dst[o + 2] = uchar(sr / div);
            dst[o + 3] = uchar(sa / div);
            const int addIdx = std::clamp(y + r + 1, 0, h - 1);
            const int remIdx = std::clamp(y - r, 0, h - 1);
            const int ai = addIdx * stride + col, ri = remIdx * stride + col;
            sb += src[ai] - src[ri];
            sg += src[ai + 1] - src[ri + 1];
            sr += src[ai + 2] - src[ri + 2];
            sa += src[ai + 3] - src[ri + 3];
        }
    }
}

} // namespace poptools
