#pragma once

#include <QDateTime>
#include <QImage>
#include <QString>

namespace poptools {

// ============================================================
// 当前壁纸读取 + 毛玻璃模糊（照搬 rlqd CalendarClone/WallpaperProvider.cs）：
// 注册表壁纸路径 → TranscodedWallpaper → CachedFiles → Spotlight 候选，
// 按系统 WallpaperStyle 铺排，1/4 降采样 + 双向 BoxBlur（r=10, 2 遍）后放大。
// 20s 缓存；30s 定时刷新由宿主控制器驱动。
// ============================================================
class WallpaperProvider
{
public:
    WallpaperProvider(int width, int height);

    QImage blurredWallpaper(); // 缓存 20s 内直接返回
    void refresh();
    void dispose() { m_blurred = QImage(); }

private:
    QString wallpaperPath() const;
    QImage renderWallpaper(const QImage &src) const;
    static QImage boxBlur(const QImage &src, int radius, int passes);
    static void boxBlurH(const uchar *src, uchar *dst, int w, int h, int stride, int r);
    static void boxBlurV(const uchar *src, uchar *dst, int w, int h, int stride, int r);

    QImage m_blurred;
    QDateTime m_lastRefresh;
    QString m_lastPath;
    int m_w = 0;
    int m_h = 0;
};

} // namespace poptools
