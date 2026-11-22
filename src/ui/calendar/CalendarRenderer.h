#pragma once

#include <QDate>
#include <QFontMetrics>
#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QSize>

#include "core/calendar/CalendarLayout.h"
#include "core/calendar/CalendarSettings.h"
#include "core/calendar/CalendarTaskStore.h"
#include "ui/shared/DesktopWallpaperProvider.h"

class QPainter;

namespace poptools {

class DesktopSharedCanvas;

// Goody 工作摘要视图（T-067；2026-11-22 用户拍板改道：格内链接撤除，日报
// 入口移至日程编辑器标题行）：月报链接绘制的唯一数据源，由面板按显示月
// 缓存（GoodyModule 查询槽构建，goodySummariesChanged/翻月时刷新）
struct CalendarDigestView
{
    QHash<QDate, QString> daily; // 有摘要的日期 → md 路径（面板→编辑器取数用）
    QString monthly;             // 当月月报 md 路径（空=未生成，链接不画）
};

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

    // 绘制整幅画面并返回本次布局（供命中测试使用）；digests 为摘要链接视图
    //（空视图 = 不画链接，调整预览路径用默认值）
    CalendarLayout render(QPainter *g, DesktopSharedCanvas &canvas, const CalendarSettings &settings,
                          const CalendarTaskStore &store, const QDate &today, int year, int month,
                          const QPoint &hover, const QString &hoverHint, bool adjusting,
                          const CalendarDigestView &digests = CalendarDigestView());

    // T-068 异步壁纸三件套：尺寸查询（线程池任务入参）/ 是否需重算（签名未变
    // 免算）/ 主线程收成品。重算本体 DesktopWallpaperProvider::computeBlurred
    // 是静态函数，由面板经线程池调度（T-071：同步 refresh 已删，重算一律
    // 走宿主 kick 的异步池路径，渲染取图只回缓存）
    QSize wallpaperSize() const { return m_wallpaper.canvasSize(); }
    bool wallpaperRefreshNeeded() { return m_wallpaper.refreshNeeded(); }
    void adoptWallpaper(const QImage &img) { m_wallpaper.adoptFrom(img); }

private:
    // C# 字号是磅值（96dpi 下 px = pt*4/3），画布为物理像素，再乘 DPR 折算（缓存）
    QFont font(qreal pointSize, bool bold = false) const;
    // 按字体的像素字号取缓存 QFontMetrics（T-071：一次渲染 ~100 处
    // QFontMetrics(f) 逐处构造 → 按键复用；键折叠同 font()）
    const QFontMetrics &fontMetricsFor(const QFont &f) const;
    // 完成态日程的删除线派生字体（按像素字号缓存，免逐行 detach 重解析）
    QFont struckFont(const QFont &base) const;
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
    mutable QHash<int, QFontMetrics> m_fontMetricsCache; // 键同 font() 折叠（T-071）
    mutable QHash<int, QFont> m_strikeCache;             // 键 = 像素字号（T-071）
};

} // namespace poptools
