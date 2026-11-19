#pragma once

#include <QDateTime>
#include <QImage>
#include <QSize>
#include <QString>

namespace poptools {

// ============================================================
// 当前壁纸读取 + 毛玻璃模糊（照搬 rlqd CalendarClone/DesktopWallpaperProvider.cs）：
// 注册表壁纸路径 → TranscodedWallpaper → CachedFiles → Spotlight 候选，
// 按系统 WallpaperStyle 铺排，1/4 降采样 + 双向 BoxBlur（r=10, 2 遍）后放大。
// 20s 缓存；30s 定时刷新由宿主控制器驱动。
//
// T-068 主线程停摆根治：重算（磁盘整图解码 + 两遍盒模糊）为 100~200ms 级，
// 本进程主线程挂着 WH_MOUSE_LL / WH_KEYBOARD_LL 低级钩子，主线程停摆即全
// 系统鼠标排队（用户感知"拖动卡一下再瞬移"）。两条措施：
//   ① 签名免重算——候选文件签名（路径/修改时间/大小 + 壁纸风格）未变时
//      refresh/refreshNeeded 只续 20s 时基，零解码零模糊；
//   ② 重算可离主线程——computeBlurred 静态无状态（线程池可调），成品经
//      adoptFrom 回装缓存（宿主 30s 定时器/duty 启动即踢走此异步路径）。
// ============================================================
class DesktopWallpaperProvider
{
public:
    DesktopWallpaperProvider(int width, int height);

    // 渲染取图：只回缓存（20s 新鲜直回；过期签名未变续时基；真变了回旧图/
    // 空图）——绝不同步解码重算（T-068/T-071，红线①），重算走宿主 30s 定时
    // 器的异步池路径
    QImage blurredWallpaper();
    // 释放缓存省内存；下次异步刷新（宿主 kick → computeBlurred）重算
    void dispose() { m_blurred = QImage(); }

    // 是否需要重算（缓存新鲜或签名未变 → 只续 20s 时基并返回 false）。
    // 廉价探测：不做任何图片解码，只比对候选文件签名
    bool refreshNeeded();
    // 供线程池调用的完整重算（磁盘解码 → 铺排 → 降采样 → 模糊 → 放大）。
    // 无共享状态（尺寸入参），任意线程可调；100~200ms 级耗时
    static QImage computeBlurred(int width, int height, QString *pathOut = nullptr);
    // 主线程收成品：装缓存 + 续时基 + 记签名（签名内部廉价重探，不解码）
    void adoptFrom(const QImage &img);

    QSize canvasSize() const { return QSize(m_w, m_h); }

private:
    static QString wallpaperPath();
    static QString wallpaperSignature();
    static QImage renderWallpaper(const QImage &src, int width, int height);
    static QImage boxBlur(const QImage &src, int radius, int passes);
    static void boxBlurH(const uchar *src, uchar *dst, int w, int h, int stride, int r);
    static void boxBlurV(const uchar *src, uchar *dst, int w, int h, int stride, int r);

    QImage m_blurred;
    QDateTime m_lastRefresh;
    QString m_lastSignature; // 上次成品的候选文件签名；未变即免重算
    int m_w = 0;
    int m_h = 0;
};

} // namespace poptools
