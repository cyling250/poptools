#pragma once

#include <QFontMetrics>
#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QSize>
#include <QVector>

#include "core/box/BoxLayout.h"
#include "core/box/BoxCatalog.h"
#include "ui/shared/DesktopWallpaperProvider.h"

class QPainter;

namespace poptools {

// 盒内条目（文件夹门户：即目录项的视图快照）
struct BoxItem
{
    QString name;   // 文件名（含扩展名）
    QString target; // 绝对路径
    bool isDir = false;
};

// ============================================================
// 收纳盒渲染器（与 CalendarRenderer 同一视觉家族：毛玻璃壁纸底 +
// 深色玻璃 + 圆角面板 + 投影；内容为图标网格 + 标签 + 翻页控件）。
// render() 画进面板私有暂存帧（宿主合成后原子发布）；槽位矩形由
// 表面宿主统一写（渲染器不再碰共享头）。
// ============================================================
class BoxRenderer
{
public:
    BoxRenderer(int width, int height, qreal dpr = 1.0);
    ~BoxRenderer();
    Q_DISABLE_COPY(BoxRenderer)

    // layout 由面板算好（尺寸纪律在 BoxLayout::compute，渲染器只画不布局）；
    // items：当前盒的条目快照；icons：target → 已提取图标（未命中画首字符
    // 占位块，调用方后台提取完成后再次渲染）
    BoxLayout render(QPainter *g, const BoxDef &def, const BoxLayout &layout,
                     const QVector<BoxItem> &items, const QPoint &hover, int selected,
                     const QHash<QString, QImage> &icons);

    // T-068 异步壁纸三件套（与 CalendarRenderer 同款，见彼处注释；
    // T-071：同步 refresh 已删，重算一律走宿主 kick 的异步池路径）
    QSize wallpaperSize() const { return m_wallpaper.canvasSize(); }
    bool wallpaperRefreshNeeded() { return m_wallpaper.refreshNeeded(); }
    void adoptWallpaper(const QImage &img) { m_wallpaper.adoptFrom(img); }

private:
    QFont font(qreal pointSize, bool bold = false) const;
    // 按字体的像素字号取缓存 QFontMetrics（T-071，与 CalendarRenderer 同款）
    const QFontMetrics &fontMetricsFor(const QFont &f) const;
    void drawShadow(QPainter *g, const QRect &panel);
    void drawShadowText(QPainter *g, const QString &text, const QFont &f, const QColor &color,
                        const QPointF &pos);
    void drawHeaderIcon(QPainter *g, BoxHeaderKind kind, const QRect &r, bool enabled);

    static QPainterPath roundedPath(const QRectF &r, qreal radius);

    DesktopWallpaperProvider m_wallpaper;
    int m_width = 0;
    int m_height = 0;
    qreal m_dpr = 1.0;
    mutable QHash<int, QFont> m_fontCache; // 键 = 像素字号 + bold×1e6（见 font() 键折叠）
    mutable QHash<int, QFontMetrics> m_fontMetricsCache; // 键同 font() 折叠（T-071）
};

} // namespace poptools
