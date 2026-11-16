#include "BoxRenderer.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace poptools {

// 记录画布尺寸与 DPR（字号 px 折算、图标缩放、壁纸 provider 同尺寸基准）
BoxRenderer::BoxRenderer(int width, int height, qreal dpr)
    : m_wallpaper(width, height)
    , m_width(width)
    , m_height(height)
    , m_dpr(dpr)
{
}

BoxRenderer::~BoxRenderer() = default;

QPainterPath BoxRenderer::roundedPath(const QRectF &r, qreal radius)
{
    QPainterPath path;
    const qreal d = std::min(radius * 2, std::min(r.width(), r.height()));
    path.moveTo(r.left(), r.top() + d);
    path.arcTo(QRectF(r.left(), r.top(), d, d), 180, -90);
    path.lineTo(r.right() - d, r.top());
    path.arcTo(QRectF(r.right() - d, r.top(), d, d), 90, -90);
    path.lineTo(r.right(), r.bottom() - d);
    path.arcTo(QRectF(r.right() - d, r.bottom() - d, d, d), 0, -90);
    path.lineTo(r.left() + d, r.bottom());
    path.arcTo(QRectF(r.left(), r.bottom() - d, d, d), 270, -90);
    path.closeSubpath();
    return path;
}

QFont BoxRenderer::font(qreal pointSize, bool bold) const
{
    // 与日历同口径：磅值 × 4/3 × DPR 折算成画布物理像素字号。
    // 键折叠：像素字号 + bold×1e6 折进整数键（px 实际 ≤ 数十，无碰撞）
    const int px = qRound(pointSize * 4.0 / 3.0 * m_dpr);
    const int key = px + (bold ? 1000000 : 0);
    const auto it = m_fontCache.constFind(key);
    if (it != m_fontCache.constEnd())
        return it.value();
    QFont f;
    f.setFamilies({ QStringLiteral("Microsoft YaHei UI"), QStringLiteral("SimSun") });
    f.setPixelSize(px);
    f.setBold(bold);
    m_fontCache.insert(key, f);
    return f;
}

// 面板外圈多层圆角描边模拟柔和投影（由外向内、alpha 恒 6 叠深；与日历同一配方）
void BoxRenderer::drawShadow(QPainter *g, const QRect &panel)
{
    for (int i = 18; i >= 1; i -= 2) {
        QPen pen(QColor(0, 0, 0, 6), 4);
        pen.setJoinStyle(Qt::RoundJoin);
        g->setPen(pen);
        g->setBrush(Qt::NoBrush);
        g->drawPath(roundedPath(QRectF(panel.adjusted(-i, -i, i, i)), 12));
    }
}

// 文字 + (1,1) 偏移黑影双层绘制：壁纸上的可读性兜底（不用 QPaintEffect，成本最低）
void BoxRenderer::drawShadowText(QPainter *g, const QString &text, const QFont &f,
                                 const QColor &color, const QPointF &pos)
{
    QFontMetrics fm(f);
    g->setFont(f);
    g->setPen(QColor(0, 0, 0, 120));
    g->drawText(QPointF(pos.x() + 1, pos.y() + 1), text);
    g->setPen(color);
    g->drawText(QPointF(pos.x(), pos.y()), text);
}

// 头部按钮矢量图标：‹/› 翻页箭头或 ✛ 十字箭头；enabled=false 降透明度（灰）表示不可翻页
void BoxRenderer::drawHeaderIcon(QPainter *g, BoxHeaderKind kind, const QRect &r, bool enabled)
{
    const QColor c = enabled ? QColor(226, 226, 226, 220) : QColor(160, 160, 160, 70);
    QPen pen(c, 2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    g->setPen(pen);
    g->setBrush(Qt::NoBrush);
    const QPointF center = QRectF(r).center();
    if (kind == BoxHeaderKind::Prev || kind == BoxHeaderKind::Next) {
        // 页箭头 ‹ ›
        const qreal dir = kind == BoxHeaderKind::Prev ? -1.0 : 1.0;
        const QPointF tip = center + QPointF(dir * 4.0 * m_dpr, 0);
        g->drawPolyline(QVector<QPointF>{ tip + QPointF(-dir * 5.0 * m_dpr, -5.0 * m_dpr),
                                          tip, tip + QPointF(-dir * 5.0 * m_dpr, 5.0 * m_dpr) });
    } else {
        // 调整：十字箭头（与日历头部 Adjust 同款）
        g->drawLine(QPointF(center.x() - 7 * m_dpr, center.y()),
                    QPointF(center.x() + 7 * m_dpr, center.y()));
        g->drawLine(QPointF(center.x(), center.y() - 7 * m_dpr),
                    QPointF(center.x(), center.y() + 7 * m_dpr));
        g->drawPolyline(QVector<QPointF>{ QPointF(center.x() - 4 * m_dpr, center.y() - 3 * m_dpr),
                                          QPointF(center.x() - 7 * m_dpr, center.y()),
                                          QPointF(center.x() - 4 * m_dpr, center.y() + 3 * m_dpr) });
        g->drawPolyline(QVector<QPointF>{ QPointF(center.x() + 4 * m_dpr, center.y() - 3 * m_dpr),
                                          QPointF(center.x() + 7 * m_dpr, center.y()),
                                          QPointF(center.x() + 4 * m_dpr, center.y() + 3 * m_dpr) });
        g->drawPolyline(QVector<QPointF>{ QPointF(center.x() - 3 * m_dpr, center.y() - 4 * m_dpr),
                                          QPointF(center.x(), center.y() - 7 * m_dpr),
                                          QPointF(center.x() + 3 * m_dpr, center.y() - 4 * m_dpr) });
        g->drawPolyline(QVector<QPointF>{ QPointF(center.x() - 3 * m_dpr, center.y() + 4 * m_dpr),
                                          QPointF(center.x(), center.y() + 7 * m_dpr),
                                          QPointF(center.x() + 3 * m_dpr, center.y() + 4 * m_dpr) });
    }
}

BoxLayout BoxRenderer::render(QPainter *g, const BoxDef &def, const BoxLayout &layout,
                              const QVector<BoxItem> &items, const QPoint &hover, int selected,
                              const QHash<QString, QImage> &icons)
{
    // 布局由面板传入（尺寸纪律统一在 BoxLayout::compute，渲染器只画不布局）
    g->setClipRect(0, 0, m_width, m_height);
    g->setRenderHint(QPainter::Antialiasing);
    g->setRenderHint(QPainter::TextAntialiasing);
    g->setRenderHint(QPainter::SmoothPixmapTransform);
    // 暂存帧私有，可就地整帧清透明（P-062 纪律只约束共享位图：发布仍走
    // commitSurface 原子拷贝）
    g->setCompositionMode(QPainter::CompositionMode_Source);
    g->fillRect(QRect(0, 0, m_width, m_height), Qt::transparent);
    g->setCompositionMode(QPainter::CompositionMode_SourceOver);

    if (def.hidden) {
        // 隐藏盒也要清画布：共享位图是持久的，不清则桌面残留旧帧
        g->setCompositionMode(QPainter::CompositionMode_Source);
        g->fillRect(QRect(0, 0, m_width, m_height), Qt::transparent);
        return layout;
    }

    // 投影 + 玻璃面板（与日历同一配方：壁纸模糊底 + 深色玻璃 + 顶部渐变）
    drawShadow(g, layout.panel);
    const QPainterPath panelPath = roundedPath(QRectF(layout.panel), 8);
    g->save();
    g->setClipPath(panelPath);
    // 毛玻璃壁纸底（T-060 重写时曾遗漏——纯黑面板+浮空白条即此故）
    const QImage bg = m_wallpaper.blurredWallpaper();
    if (!bg.isNull())
        g->drawImage(0, 0, bg);
    g->fillRect(layout.panel, QColor(6, 6, 14, 190));
    QLinearGradient grad(layout.panel.left(), layout.panel.top(), layout.panel.left(),
                         layout.panel.top() + std::min(180, layout.panel.height()));
    grad.setColorAt(0.0, QColor(0, 0, 0, 70));
    grad.setColorAt(1.0, QColor(0, 0, 0, 0));
    g->fillRect(QRectF(layout.panel.left(), layout.panel.top(), layout.panel.width(),
                       std::min(180, layout.panel.height())),
                grad);
    g->restore();

    const float scale = 1.0f; // 字号倍率（无量纲；v1 暂不暴露字号设置）
    const QFont titleFont = font(10.0f * scale, true);
    const QFont labelFont = font(8.4f * scale);
    // 38 逻辑 px → 150% 下 57 物理像素；与 BoxLayout::kCellH=112 的预算注释
    //（10+57+标签）一致
    const int iconSize = qRound(38.0 * m_dpr);

    // 标题（盒名，单行省略）
    {
        QFontMetrics tfm(titleFont);
        const QString title = QFontMetrics(titleFont).elidedText(
            def.name, Qt::ElideRight, layout.titleRect.width());
        drawShadowText(g, title, titleFont, QColor(250, 250, 250, 255),
                       QPointF(layout.titleRect.left(),
                               layout.titleRect.top()
                                   + (layout.titleRect.height() - tfm.height()) / 2.0));
    }

    // 头部按钮（含悬停底色）
    {
        const int hh = layout.hitHeader(hover);
        const bool canPrev = layout.page > 0;
        const bool canNext = layout.page < layout.pageCount - 1;
        for (int i = 0; i < layout.headerButtons.size(); ++i) {
            const QRect r = layout.headerButtons.at(i);
            if (i == hh) {
                g->fillPath(roundedPath(QRectF(r.adjusted(-1, -1, 1, 1)), 4),
                            QColor(255, 255, 255, 34));
            }
            const bool enabled = i == int(BoxHeaderKind::Prev) ? canPrev
                               : i == int(BoxHeaderKind::Next) ? canNext
                                                               : true;
            drawHeaderIcon(g, BoxHeaderKind(i), r, enabled);
        }
    }

    // 图标网格（当前页）；可见区间口径 = layout.firstVisibleIndex/visibleCount（勿双写）
    const int first = layout.firstVisibleIndex();
    const int visible = layout.visibleCount(items.size());
    // hover 循环不变，命中下标提出循环免逐格重算（纯循环不变量外提）
    const int hoveredCell = layout.hitCell(hover);
    for (int i = 0; i < visible; ++i) {
        const QRect cell = layout.cells.at(i);
        const int idx = first + i;
        const bool hovered = hoveredCell == i;
        const bool isSelected = idx == selected;
        if (hovered || isSelected) {
            const QRect bg = cell.adjusted(3, 3, -3, -3);
            // 选中底色比悬停深一档；同格同时选中+悬停时取选中色
            g->fillPath(roundedPath(QRectF(bg), 6),
                        QColor(255, 255, 255, isSelected ? 58 : 34));
        }
        // 图标（缓存未命中画首字符占位块，后台提取完成后面板会再渲染）
        const BoxItem &e = items.at(idx);
        const QImage icon = icons.value(e.target);
        const QRect iconRect(cell.left() + (cell.width() - iconSize) / 2,
                             cell.top() + 10, iconSize, iconSize);
        if (!icon.isNull()) {
            g->drawImage(iconRect, icon);
        } else {
            const QRect ph = iconRect.adjusted(4, 4, -4, -4);
            g->fillPath(roundedPath(QRectF(ph), 6), QColor(255, 255, 255, 22));
            g->setPen(QColor(235, 235, 235, 200));
            g->setFont(labelFont);
            const QString initial = e.name.left(1).toUpper();
            g->drawText(QRectF(ph), Qt::AlignCenter, initial);
        }
        // 标签（单行省略，阴影字）。基线 = 图标底 + 间距 + ascent（T-061：旧式
        // “iconSize+14” 漏了图标顶偏移与 ascent，文字顶叠进图标底缘）；
        // 极端 DPI 兜底：文字底（含 descent）不许压出格子
        QFontMetrics lfm(labelFont);
        const QString label = lfm.elidedText(e.name, Qt::ElideRight, cell.width() - 10);
        const qreal lw = lfm.horizontalAdvance(label);
        const qreal baseline =
            std::min(qreal(iconRect.bottom() + 1 + qRound(7.0 * m_dpr) + lfm.ascent()),
                     qreal(cell.bottom()) - lfm.descent() - 2.0);
        drawShadowText(g, label, labelFont, QColor(235, 235, 235, 255),
                       QPointF(cell.left() + (cell.width() - lw) / 2.0, baseline));
    }

    // 空盒提示
    if (items.isEmpty()) {
        QFont hintFont = font(8.6f * scale);
        g->setFont(hintFont);
        g->setPen(QColor(200, 200, 200, 190));
        const int hintY = layout.headerBar.bottom() + 1
                          + (layout.rows * BoxLayout::kCellH) / 2;
        g->drawText(QRectF(layout.panel.left(), hintY - 20, layout.panel.width(), 40),
                    Qt::AlignHCenter | Qt::AlignVCenter,
                    // T-062 修复: 空盒提示原指向 T-059 已移除的"⚙ 打开所在文件夹"
                    // 入口（现盒头中键是 ✛ 调整），改为指向管理器的真实路径（BUG-05）
                    QStringLiteral("把文件放进绑定的文件夹\n（托盘右键 → 收纳盒管理 → 打开文件夹）"));
    }

    // 页指示：≤9 页画圆点，否则「n/m」文本
    if (layout.pageCount > 1) {
        if (!layout.dots.isEmpty()) {
            for (int i = 0; i < layout.dots.size(); ++i) {
                QColor c = i == layout.page ? QColor(240, 240, 240, 235)
                                            : QColor(240, 240, 240, 70);
                g->fillPath(roundedPath(QRectF(layout.dots.at(i)), 4), c);
            }
        } else {
            QFont pageFont = font(8.0f * scale);
            const QString text =
                QStringLiteral("%1/%2").arg(layout.page + 1).arg(layout.pageCount);
            g->setFont(pageFont);
            g->setPen(QColor(235, 235, 235, 160));
            g->drawText(QRectF(layout.panel.left(), layout.panel.bottom() + 1
                                                     - BoxLayout::kDotsHeight,
                               layout.panel.width(), BoxLayout::kDotsHeight),
                        Qt::AlignHCenter | Qt::AlignVCenter, text);
        }
    }

    return layout;
}

} // namespace poptools
