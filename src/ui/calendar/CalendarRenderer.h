#pragma once

#include <QDate>
#include <QHash>
#include <QImage>
#include <QPainterPath>

#include "core/calendar/CalendarLayout.h"
#include "core/calendar/CalendarSettings.h"
#include "core/calendar/CalendarTaskStore.h"
#include "WallpaperProvider.h"

class QPainter;

namespace poptools {

class CalendarSharedCanvas;

// ============================================================
// 日历渲染器（照搬 rlqd CalendarClone/CalendarRenderer.cs，GDI+ → QPainter）：
// 毛玻璃壁纸底 + 深色玻璃 + 月份格子（九宫格皮肤贴图）+ 农历/节气/节日/
// 日程/今日高亮/周数/标题/头部图标/引导气泡/调整模式装饰。
// render() 输出直接画进共享内存画布，随后 markReady + 通知 DLL 重绘。
// ============================================================
class CalendarRenderer
{
public:
    CalendarRenderer(int width, int height, qreal dpr = 1.0);
    ~CalendarRenderer();
    Q_DISABLE_COPY(CalendarRenderer)

    // 绘制整幅画面并返回本次布局（供命中测试使用）
    CalendarLayout render(QPainter *g, CalendarSharedCanvas &canvas, const CalendarSettings &settings,
                          const CalendarTaskStore &store, const QDate &today, int year, int month,
                          const QPoint &hover, const QString &hoverHint, bool adjusting);

    void refreshWallpaper() { m_wallpaper.refresh(); }

private:
    QFont font(qreal pointSize, bool bold = false) const;
    void drawShadow(QPainter *g, const QRect &panel);
    void drawShadowText(QPainter *g, const QString &text, const QFont &f, const QColor &color,
                        const QPointF &pos, qreal maxWidth = 0);
    void drawShadowTextInRect(QPainter *g, const QString &text, const QFont &f,
                              const QColor &color, const QRectF &rect, Qt::Alignment align);
    void drawNineSlice(QPainter *g, const QImage &img, const QRect &dst, int border);
    void drawNineSliceBorder(QPainter *g, const QImage &img, const QRect &dst, int srcBorder,
                             int destBorder);
    void drawHeaderIcon(QPainter *g, HeaderButtonKind kind, const QRect &r);

    static QImage darkenImage(const QImage &src, float mul, float alphaMul);
    static QImage loadAsset(const QString &qrcPath);
    static QPainterPath roundRectPath(const QRectF &r, qreal radius);

    WallpaperProvider m_wallpaper;
    QImage m_cellBg;
    QImage m_cellBgDark;
    QImage m_breakBg;
    QImage m_todayGlow;
    int m_width = 0;
    int m_height = 0;
    qreal m_dpr = 1.0;
    mutable QHash<qreal, QFont> m_fontCache;
};

} // namespace poptools
