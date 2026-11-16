#pragma once

#include <QDate>
#include <QHash>
#include <QImage>
#include <QPainterPath>

#include "core/calendar/CalendarLayout.h"
#include "core/calendar/CalendarSettings.h"
#include "core/calendar/CalendarTaskStore.h"
#include "ui/shared/DesktopWallpaperProvider.h"

class QPainter;

namespace poptools {

class DesktopSharedCanvas;

// ============================================================
// 日历渲染器（照搬 rlqd CalendarClone/CalendarRenderer.cs，GDI+ → QPainter）：
// 毛玻璃壁纸底 + 深色玻璃 + 月份格子（九宫格皮肤贴图）+ 农历/节气/节日/
// 日程/今日高亮/周数/标题/头部图标/引导气泡/调整模式装饰。
// render() 画进调用方给定的暂存帧；共享位图发布、槽位与 ready 标记由
// DesktopSurfaceHost::commitSurface 统一负责（T-062 G-01/S-09：渲染器不再
// 直写 canvas——pre-T-059 直渲架构残留，把"槽 0=日历"的注册顺序依赖固化进了渲染器）。
// ============================================================
class CalendarRenderer
{
public:
    CalendarRenderer(int width, int height, qreal dpr = 1.0);
    ~CalendarRenderer();
    Q_DISABLE_COPY(CalendarRenderer)

    // 绘制整幅画面并返回本次布局（供命中测试使用）
    CalendarLayout render(QPainter *g, DesktopSharedCanvas &canvas, const CalendarSettings &settings,
                          const CalendarTaskStore &store, const QDate &today, int year, int month,
                          const QPoint &hover, const QString &hoverHint, bool adjusting);

    void refreshWallpaper() { m_wallpaper.refresh(); }

private:
    // C# 字号是磅值（96dpi 下 px = pt*4/3），画布为物理像素，再乘 DPR 折算（缓存）
    QFont font(qreal pointSize, bool bold = false) const;
    // 面板外扩 9 圈描边模拟投影（配方与收纳盒同族，见 box-r1 C-03）
    void drawShadow(QPainter *g, const QRect &panel);
    // 带阴影文字：pos 为文字左上角（GDI+ DrawString 锚点语义）；maxWidth>0 走
    // 矩形裁切省略路径
    void drawShadowText(QPainter *g, const QString &text, const QFont &f, const QColor &color,
                        const QPointF &pos, qreal maxWidth = 0);
    // 带阴影文字（矩形锚）：文字在 rect 内按 align 摆放，不做手算居中补偿（星期表头用）
    void drawShadowTextInRect(QPainter *g, const QString &text, const QFont &f,
                              const QColor &color, const QRectF &rect, Qt::Alignment align);
    // 九宫格贴图统一实现（原 drawNineSlice/drawNineSliceBorder 合并，S-02）：
    // 四角原样 + 边/中心拉伸；withCenter=false 只画 8 块环带不画中心
    void drawNinePatch(QPainter *g, const QImage &img, const QRect &dst, int srcBorder,
                       int destBorder, bool withCenter);
    // 三种头部按钮的矢量图标（无贴图）
    void drawHeaderIcon(QPainter *g, HeaderButtonKind kind, const QRect &r);

    static QImage darkenImage(const QImage &src, float mul, float alphaMul);
    // qrc 加载并转 ARGB32_Premultiplied，失败返回空图
    static QImage loadAsset(const QString &qrcPath);

    DesktopWallpaperProvider m_wallpaper;
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
