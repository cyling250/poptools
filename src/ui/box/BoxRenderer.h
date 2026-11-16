#pragma once

#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QVector>

#include "core/box/BoxLayout.h"
#include "core/box/BoxCatalog.h"
#include "ui/shared/DesktopSharedCanvas.h"
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

    void refreshWallpaper() { m_wallpaper.refresh(); }

private:
    QFont font(qreal pointSize, bool bold = false) const;
    void drawShadow(QPainter *g, const QRect &panel);
    void drawShadowText(QPainter *g, const QString &text, const QFont &f, const QColor &color,
                        const QPointF &pos);
    void drawHeaderIcon(QPainter *g, BoxHeaderKind kind, const QRect &r, bool enabled);

    static QPainterPath roundedPath(const QRectF &r, qreal radius);

    DesktopWallpaperProvider m_wallpaper;
    int m_width = 0;
    int m_height = 0;
    qreal m_dpr = 1.0;
    mutable QHash<qreal, QFont> m_fontCache;
};

} // namespace poptools
