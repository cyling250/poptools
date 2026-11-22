#include "CalendarRenderer.h"

#include "CalendarPaintUtils.h" // 圆角矩形路径（S-03 模块内收敛）
#include "ui/shared/DesktopSharedCanvas.h"
#include "ui/ThemeColors.h" // overlay::warmAccent（T-071 橙收敛）
#include "core/calendar/ChineseCalendar.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>

#include <cmath>

namespace poptools {

CalendarRenderer::CalendarRenderer(int width, int height, qreal dpr)
    : m_wallpaper(width, height)
    , m_width(width)
    , m_height(height)
    , m_dpr(dpr)
{
    m_cellBg = loadAsset(QStringLiteral(":/calendar/9GridElementBkgnd.png"));
    m_cellBgDark = darkenImage(m_cellBg, 0.8f, 0.6f);
    m_breakBg = loadAsset(QStringLiteral(":/calendar/9GridEleBreakDay.png"));
    m_todayGlow = loadAsset(QStringLiteral(":/calendar/9GridEleTodayHighlight-60x60.png"));
}

CalendarRenderer::~CalendarRenderer() = default;

QImage CalendarRenderer::loadAsset(const QString &qrcPath)
{
    QImage img(qrcPath);
    return img.isNull() ? QImage() : img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// 对齐 GDI+ ColorMatrix：R*mul / G*mul*0.55 / B*mul*0.80 / A*alphaMul
QImage CalendarRenderer::darkenImage(const QImage &src, float mul, float alphaMul)
{
    if (src.isNull())
        return QImage();
    QImage out(src.width(), src.height(), QImage::Format_ARGB32);
    QImage in = src.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < in.height(); ++y) {
        const QRgb *inLine = reinterpret_cast<const QRgb *>(in.constScanLine(y));
        QRgb *outLine = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < in.width(); ++x) {
            const QRgb c = inLine[x];
            const int r = int(qRed(c) * mul);
            const int g = int(qGreen(c) * mul * 0.55f);
            const int b = int(qBlue(c) * mul * 0.80f);
            const int a = int(qAlpha(c) * alphaMul);
            outLine[x] = qRgba(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255),
                               qBound(0, a, 255));
        }
    }
    return out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QFont CalendarRenderer::font(qreal pointSize, bool bold) const
{
    // C# 字号是磅值（96dpi 下 px = pt*4/3），画布为物理像素，再乘 DPR 折算
    const int px = qRound(pointSize * 4.0 / 3.0 * m_dpr);
    const auto it = m_fontCache.constFind(px + (bold ? 1000000 : 0));
    if (it != m_fontCache.constEnd())
        return it.value();
    QFont f;
    f.setFamilies({ QStringLiteral("SimSun"), QStringLiteral("Microsoft YaHei UI") });
    f.setPixelSize(px);
    f.setBold(bold);
    m_fontCache.insert(px + (bold ? 1000000 : 0), f);
    return f;
}

const QFontMetrics &CalendarRenderer::fontMetricsFor(const QFont &f) const
{
    // 键折叠与 font() 同口径（像素字号 + bold×1e6）
    const int key = f.pixelSize() + (f.bold() ? 1000000 : 0);
    auto it = m_fontMetricsCache.constFind(key);
    if (it == m_fontMetricsCache.constEnd())
        it = m_fontMetricsCache.insert(key, QFontMetrics(f));
    return it.value();
}

QFont CalendarRenderer::struckFont(const QFont &base) const
{
    const int key = base.pixelSize();
    auto it = m_strikeCache.constFind(key);
    if (it == m_strikeCache.constEnd()) {
        QFont f = base;
        f.setStrikeOut(true);
        it = m_strikeCache.insert(key, f);
    }
    return it.value();
}

CalendarLayout CalendarRenderer::render(QPainter *g, DesktopSharedCanvas &canvas,
                                        const CalendarSettings &settings,
                                        const CalendarTaskStore &store, const QDate &today,
                                        int year, int month, const QPoint &hover,
                                        const QString &hoverHint, bool adjusting,
                                        const CalendarDigestView &digests)
{
    const QRect panel = settings.rect();
    const CalendarLayout layout(panel, year, month);
    g->setClipRect(0, 0, m_width, m_height);
    g->setRenderHint(QPainter::Antialiasing);
    g->setRenderHint(QPainter::TextAntialiasing);
    g->setRenderHint(QPainter::SmoothPixmapTransform);
    g->setCompositionMode(QPainter::CompositionMode_Source);
    g->fillRect(QRect(0, 0, m_width, m_height), Qt::transparent);
    g->setCompositionMode(QPainter::CompositionMode_SourceOver);

    if (settings.hidden) {
        // T-062 修复: G-01/S-09 删除 renderer 直写 canvas（setSlotRect/markReady）——
        // 槽位/ready/发布全权归 DesktopSurfaceHost::commitSurface（它在 render 末尾
        // 被调用，无条件重写全部槽位并 markReady；hidden 时宿主照常发布本面板的
        // 透明帧）。pre-T-059 直渲架构残留，还把"槽 0=日历"的注册顺序依赖固化进了
        // 渲染器（硬编码槽 0）
        return layout;
    }

    drawShadow(g, panel);
    const QPainterPath panelPath = roundedPath(QRectF(panel), 8);
    g->save();
    g->setClipPath(panelPath);
    if (settings.glassEffect) {
        const QImage bg = m_wallpaper.blurredWallpaper();
        if (!bg.isNull())
            g->drawImage(0, 0, bg);
        g->fillRect(panel, QColor(6, 6, 14, 182));
        QLinearGradient grad(panel.left(), panel.top(), panel.left(),
                             panel.top() + std::min(220, panel.height()));
        grad.setColorAt(0.0, QColor(0, 0, 0, 70));
        grad.setColorAt(1.0, QColor(0, 0, 0, 0));
        g->fillRect(QRectF(panel.left(), panel.top(), panel.width(),
                           std::min(220, panel.height())),
                    grad);
    } else {
        g->fillRect(panel, QColor(22, 24, 32, 255));
    }
    g->restore();

    const float scale = std::clamp(settings.fontScale, 70, 160) / 100.0f;
    const QFont dateFont = font(11.0f * scale);
    const QFont lunarFont = font(8.2f * scale);
    const QFont eventFont = font(8.8f * scale);
    const QFont weekFont = font(8.4f * scale);
    const QFont weekHeaderFont = font(9.3f * scale);
    const QFont titleFont = font(11.5f * scale, true);
    const QFont pillFont = font(8.5f * scale);

    // 头部条 / 星期条 / 底条
    g->fillRect(QRectF(panel.left(), panel.top(), panel.width(), layout.kTitleHeight),
                QColor(0, 0, 0, 36));
    g->fillRect(QRectF(panel.left(), layout.titleBar.bottom(), panel.width(),
                       layout.kWeekHeaderHeight),
                QColor(0, 0, 0, 24));
    g->fillRect(QRectF(layout.bottomBar), QColor(0, 0, 0, 28));

    // 单元格：皮肤贴图 + 日期 + 农历 + 日程
    for (int row = 0; row < layout.rows; ++row) {
        for (int col = 0; col < 7; ++col) {
            const QRect cell = layout.cellRect(row, col);
            const QDate date = layout.cellDate(row, col);
            const bool inMonth = layout.inDisplayMonth(date);
            const DayLabel label = ChineseCalendar::dayLabel(date, settings.showLunar);
            const QImage &bg = !m_cellBgDark.isNull() ? m_cellBgDark : m_cellBg;
            if (!bg.isNull())
                drawNinePatch(g, bg, cell, 12, 12, true);
            if (!inMonth)
                continue;

            const QFontMetrics &fm = fontMetricsFor(dateFont);
            const qreal dateTextW = fm.horizontalAdvance(QString::number(date.day()));
            const QPointF datePos(cell.left() + 8, cell.top() + 3);
            drawShadowText(g, QString::number(date.day()), dateFont,
                           QColor(246, 246, 246, 255), datePos);
            qreal rowTextW = dateTextW; // 日期行已占宽（农历/记录标识顺次排布）
            if (!label.text.isEmpty()) {
                QColor lc(188, 188, 188, 255);
                switch (label.kind) {
                    case DayLabelKind::SolarTerm: lc = QColor(112, 214, 138, 255); break;
                    case DayLabelKind::Holiday:
                        lc = overlay::warmAccent(); // T-071 收编（日历家族暖强调，见 ThemeColors.h）
                        lc.setAlpha(255);
                        break;
                    case DayLabelKind::LunarMonth: lc = QColor(214, 214, 214, 255); break;
                    default: lc = QColor(188, 188, 188, 255); break;
                }
                // 农历字号更小，顶锚点再下移 3px 使两行文字基线近似对齐
                //（与 drawShadowText 顶锚语义配套）
                drawShadowText(g, label.text, lunarFont, lc,
                               datePos + QPointF(dateTextW + 6, 3));
                rowTextW += 6 + fontMetricsFor(lunarFont).horizontalAdvance(label.text);
            }
            // 「有记录」标识（T-075b 用户拍板 2026-11-22）：有 Goody 日报的日期
            // 在日期行尾跟小字标示，无记录的日子不写——扫一眼即可见哪天有记录。
            // 纯展示（点击入口在日程编辑器标题行，2026-11-22 改道）；暖橙同
            // 摘要/节日家族强调色，锚点下移 3px 与农历基线对齐
            if (digests.daily.contains(date)) {
                drawShadowText(g, QStringLiteral("有记录"), lunarFont,
                               overlay::warmAccent(),
                               datePos + QPointF(rowTextW + 6, 3),
                               cell.right() - (datePos.x() + rowTextW + 6) - 4);
            }

            if (settings.showEvents) {
                const QVector<CalendarTaskItem> tasks = store.get(date);
                if (!tasks.isEmpty()) {
                    const qreal lineH = 17 * scale; // 17 = rlqd 日程行高节奏（100% 时），随 fontScale 折算
                    g->save();
                    g->setClipRect(QRect(cell.left(), cell.top(), cell.width(), cell.height() - 2));
                    // 27 = 日期行占位高（顶 3px + 日期字高），日程首行从其下开始
                    //（2026-11-22：格内摘要链接撤除，不再让行——入口在日程编辑器）
                    qreal ey = cell.top() + 27;
                    for (int i = 0; i < tasks.size(); ++i) {
                        if (ey + lineH > cell.bottom())
                            break;
                        const CalendarTaskItem &t = tasks.at(i);
                        const QString txt =
                            t.time.isEmpty()
                                ? QStringLiteral("%1. %2").arg(i + 1).arg(t.text)
                                : QStringLiteral("%1. %2 %3").arg(i + 1).arg(t.time, t.text);
                        const QFont f = t.done ? struckFont(eventFont) : eventFont;
                        drawShadowText(g, txt, f,
                                       t.done ? QColor(150, 150, 150, 255)
                                              : QColor(236, 236, 236, 255),
                                       QPointF(cell.left() + 8, ey), cell.width() - 14);
                        ey += lineH;
                    }
                    g->restore();
                }
            }

            if (date == today) {
                // 单框方案：发光环贴图外扩 11px 绘制，让环带正压在格边上（贴图环带
                // 中心内缩 ~15.5px，经 sb20→db14 映射后落在绘制矩形内缩 ~11px 处，
                // 外扩 11px 恰好抵消）。原「外圈白线 + 内圈发光」双框废弃
                if (!m_todayGlow.isNull())
                    drawNinePatch(g, m_todayGlow, cell.adjusted(-11, -11, 11, 11), 20, 14, false);
            }
        }
    }

    // 网格线
    {
        QPen line(QColor(255, 255, 255, 30), 1);
        g->setPen(line);
        for (int c = 1; c < 7; ++c) {
            const qreal x = layout.gridContent.left() + c * layout.cellWidth;
            g->drawLine(QPointF(x, layout.gridContent.top()),
                        QPointF(x, layout.gridContent.bottom() + 1));
        }
        for (int r = 1; r < layout.rows; ++r) {
            const qreal y = layout.gridContent.top() + r * layout.cellHeight;
            g->drawLine(QPointF(layout.gridContent.left(), y),
                        QPointF(layout.gridContent.right() + 1, y));
        }
        g->drawLine(QPointF(layout.gridContent.left() - 1, layout.gridContent.top()),
                    QPointF(layout.gridContent.left() - 1, layout.gridContent.bottom() + 1));
        g->drawLine(QPointF(layout.gridContent.left(), layout.weekHeader.bottom() + 1),
                    QPointF(layout.gridContent.right() + 1, layout.weekHeader.bottom() + 1));
    }

    // 星期表头（矩形居中绘制：独立 QFontMetrics 测宽与 QPainter 实际渲染的
    // 字形宽度存在系统性偏差（SimSun 小字号尤甚），手算 (cellWidth-w)/2 会整体
    // 左偏约 10px；drawText(rect, AlignHCenter) 让测量与渲染走同一引擎）
    {
        for (int c = 0; c < 7; ++c) {
            const QDate day(2020, 1, 6 + c); // 2020-01-06 = 周一 … 01-12 = 周日
            const QString text = ChineseCalendar::weekdayName(day);
            const QRectF col(layout.gridContent.left() + c * layout.cellWidth,
                             layout.weekHeader.top() + 3, layout.cellWidth,
                             layout.kWeekHeaderHeight);
            drawShadowTextInRect(g, text, weekHeaderFont, QColor(226, 226, 226, 255), col,
                                 Qt::AlignHCenter | Qt::AlignTop);
        }
    }

    // 周数 + 当前周金色条
    {
        const int currentWeek = ChineseCalendar::weekNumber(today);
        const QFontMetrics &nfm = fontMetricsFor(weekFont);
        for (int r = 0; r < layout.rows; ++r) {
            const QDate date = layout.firstCellDate.addDays(r * 7);
            const int week = ChineseCalendar::weekNumber(date);
            const QRect cell(layout.weekNumberArea.left(),
                             layout.gridContent.top() + r * layout.cellHeight,
                             layout.kWeekNumberWidth, layout.cellHeight);
            const bool isCurrent = week == currentWeek;
            const QColor wc = isCurrent ? QColor(238, 160, 58, 255) : QColor(176, 176, 176, 255);
            if (settings.showWeekNumbers) {
                const qreal w = nfm.horizontalAdvance(QString::number(week));
                drawShadowText(g, QString::number(week), weekFont, wc,
                               QPointF(cell.left() + (cell.width() - w) / 2.0, cell.top() + 6));
            }
            if (isCurrent) {
                g->fillRect(QRectF(cell.left() + 3, cell.top() + cell.height() / 3.0, 3,
                                   std::max<qreal>(10, cell.height() / 3.0)),
                            QColor(240, 150, 40, 235));
            }
        }
    }

    // 标题
    {
        const QString title = ChineseCalendar::monthTitle(today, year, month);
        const QFontMetrics &tfm = fontMetricsFor(titleFont);
        const qreal w = tfm.horizontalAdvance(title);
        const qreal tx = panel.left() + (panel.width() - w) / 2.0;
        const qreal ty = panel.top() + (layout.kTitleHeight - tfm.height()) / 2.0;
        drawShadowText(g, title, titleFont, QColor(250, 250, 250, 255), QPointF(tx, ty));
    }

    // 月报链接（头部左上角留白区，T-067）：有月报才绘制；悬停底色同头部
    // 按钮手法，文字溢出按矩形省略
    if (!digests.monthly.isEmpty()) {
        const QRect r = layout.monthLink;
        const bool hov = r.contains(hover);
        if (hov)
            g->fillPath(roundedPath(QRectF(r.adjusted(-1, -1, 1, 1)), 4),
                        QColor(255, 255, 255, 34));
        drawShadowTextInRect(g, QStringLiteral("%1月工作摘要»").arg(month), pillFont,
                             hov ? QColor(120, 200, 255, 255) : QColor(158, 210, 255, 225),
                             QRectF(r), Qt::AlignLeft | Qt::AlignVCenter);
    }

    // 头部图标（含悬停底色）
    {
        const int hoverHeader = layout.hitHeader(hover);
        for (int i = 0; i < layout.headerButtons.size(); ++i) {
            const QRect r = layout.headerButtons.at(i);
            if (i == hoverHeader) {
                g->fillPath(roundedPath(QRectF(r.adjusted(-1, -1, 1, 1)), 4),
                            QColor(255, 255, 255, 34));
            }
            drawHeaderIcon(g, layout.headerKinds[i], r);
        }
    }

    // 引导 / 提示气泡
    {
        QString hint = hoverHint;
        if (hint.isEmpty() && settings.showGuide)
            hint = QStringLiteral("点击此处调整位置和大小");
        if (!hint.isEmpty()) {
            const QRect pillRect = layout.guidePill;
            const QPainterPath pill = roundedPath(QRectF(pillRect), 8);
            g->fillPath(pill, QColor(18, 20, 26, 225));
            g->setPen(QPen(QColor(255, 255, 255, 52), 1));
            g->setBrush(Qt::NoBrush);
            g->drawPath(pill);
            const QFontMetrics &pfm = fontMetricsFor(pillFont);
            const qreal w = pfm.horizontalAdvance(hint);
            drawShadowText(g, hint, pillFont, QColor(240, 240, 240, 255),
                           QPointF(pillRect.left() + (pillRect.width() - w) / 2.0,
                                   pillRect.top() + (pillRect.height() - pfm.height()) / 2.0));
        }
    }

    // 调整模式装饰（虚线框 + ✓/×）
    if (adjusting) {
        QPen dashPen(QColor(255, 255, 255, 230), 1.4, Qt::DashLine);
        g->setPen(dashPen);
        g->setBrush(Qt::NoBrush);
        g->drawRect(QRectF(panel.adjusted(-2, -2, 2, 2)));
        const QRect check(panel.right() - 72, panel.top() + 10, 24, 24);
        const QRect cancel(panel.right() - 40, panel.top() + 10, 24, 24);
        const QPainterPath b1 = roundedPath(QRectF(check), 4);
        const QPainterPath b2 = roundedPath(QRectF(cancel), 4);
        g->fillPath(b1, QColor(10, 10, 10, 210));
        g->fillPath(b2, QColor(10, 10, 10, 210));
        QPen solid(QColor(255, 255, 255, 255), 2);
        solid.setCapStyle(Qt::RoundCap);
        solid.setJoinStyle(Qt::RoundJoin);
        g->setPen(solid);
        g->drawPolyline(QVector<QPointF>{ QPointF(check.left() + 6, check.top() + 12),
                                          QPointF(check.left() + 10, check.top() + 17),
                                          QPointF(check.left() + 18, check.top() + 7) });
        g->drawLine(QPointF(cancel.left() + 7, cancel.top() + 7),
                    QPointF(cancel.right() - 7, cancel.bottom() - 7));
        g->drawLine(QPointF(cancel.right() - 7, cancel.top() + 7),
                    QPointF(cancel.left() + 7, cancel.bottom() - 7));
    }

    // 面板边框
    g->setPen(QPen(QColor(255, 255, 255, 28), 1));
    g->setBrush(Qt::NoBrush);
    g->drawPath(panelPath);

    // T-062 修复: G-01/S-09 同 hidden 分支——槽位矩形/ready/发布不再由 renderer
    // 直写（原 markReady 早于宿主 publishFrame，会让 explorer 在新帧尚未发布时
    // 被唤醒采样旧帧，并在第二条路径上破坏宿主"发布→槽位→markReady"次序纪律）。
    // CalendarPanel::render() 末尾调 commitSurface 统一完成
    return layout;
}

void CalendarRenderer::drawShadow(QPainter *g, const QRect &panel)
{
    // 投影配方（i=18..1 step2、alpha 6、pen 宽 4）与 BoxRenderer 同族（box-r1
    // C-03），改参数须两处对齐
    for (int i = 18; i >= 1; i -= 2) {
        QPen pen(QColor(0, 0, 0, 6), 4);
        pen.setJoinStyle(Qt::RoundJoin);
        g->setPen(pen);
        g->setBrush(Qt::NoBrush);
        g->drawPath(roundedPath(QRectF(panel.adjusted(-i, -i, i, i)), 12));
    }
}

void CalendarRenderer::drawShadowText(QPainter *g, const QString &text, const QFont &f,
                                      const QColor &color, const QPointF &pos, qreal maxWidth)
{
    const QFontMetrics &fm = fontMetricsFor(f); // T-071：缓存替代逐处构造
    g->setFont(f);
    if (maxWidth > 0) {
        const QString elided = fm.elidedText(text, Qt::ElideRight, maxWidth);
        g->setPen(QColor(0, 0, 0, 120));
        g->drawText(QRectF(pos.x() + 1, pos.y() + 1, maxWidth, fm.height() + 8),
                    Qt::AlignLeft | Qt::AlignTop, elided);
        g->setPen(color);
        g->drawText(QRectF(pos.x(), pos.y(), maxWidth, fm.height() + 8),
                    Qt::AlignLeft | Qt::AlignTop, elided);
    } else {
        // GDI+ DrawString 锚点为左上角，Qt drawText(QPointF) 锚点为基线——
        // 直接移植会整体抬高一个 ascent，日期/农历文字骑在横向网格线上；
        // 补偿 ascent 后 pos 语义与 C# 版一致（y = 文字顶）
        const qreal asc = fm.ascent();
        g->setPen(QColor(0, 0, 0, 120));
        g->drawText(pos + QPointF(1, asc + 1), text);
        g->setPen(color);
        g->drawText(pos + QPointF(0, asc), text);
    }
}

void CalendarRenderer::drawShadowTextInRect(QPainter *g, const QString &text, const QFont &f,
                                            const QColor &color, const QRectF &rect,
                                            Qt::Alignment align)
{
    g->setFont(f);
    g->setPen(QColor(0, 0, 0, 120));
    g->drawText(rect.adjusted(1, 1, 1, 1), align, text);
    g->setPen(color);
    g->drawText(rect, align, text);
}

// S-02：原 drawNineSlice（9 块，源/目标边宽相同）与 drawNineSliceBorder（8 块，
// 源/目标边宽独立）约 90% 同构，合并为此函数。行为等价：两处调用点（格子背景
// srcBorder=destBorder=12；今日光环 20/14 无中心）逐块矩形按现公式代入与原
// 实现一致（皮肤贴图均 60×60，源边钳制结果相同）
void CalendarRenderer::drawNinePatch(QPainter *g, const QImage &img, const QRect &dst,
                                     int srcBorder, int destBorder, bool withCenter)
{
    const int w = img.width(), h = img.height();
    const int sb = std::min(srcBorder, std::min(w / 2, h / 2));
    const int db = std::min(destBorder, std::min(dst.width() / 2, dst.height() / 2));
    const int dw = dst.width(), dh = dst.height();
    g->drawImage(QRect(dst.left(), dst.top(), db, db), img, QRect(0, 0, sb, sb));
    g->drawImage(QRect(dst.right() - db + 1, dst.top(), db, db), img, QRect(w - sb, 0, sb, sb));
    g->drawImage(QRect(dst.left(), dst.bottom() - db + 1, db, db), img, QRect(0, h - sb, sb, sb));
    g->drawImage(QRect(dst.right() - db + 1, dst.bottom() - db + 1, db, db), img,
                 QRect(w - sb, h - sb, sb, sb));
    g->drawImage(QRect(dst.left() + db, dst.top(), dw - 2 * db, db), img, QRect(sb, 0, w - 2 * sb, sb));
    g->drawImage(QRect(dst.left() + db, dst.bottom() - db + 1, dw - 2 * db, db), img,
                 QRect(sb, h - sb, w - 2 * sb, sb));
    g->drawImage(QRect(dst.left(), dst.top() + db, db, dh - 2 * db), img, QRect(0, sb, sb, h - 2 * sb));
    g->drawImage(QRect(dst.right() - db + 1, dst.top() + db, db, dh - 2 * db), img,
                 QRect(w - sb, sb, sb, h - 2 * sb));
    if (withCenter)
        g->drawImage(QRect(dst.left() + db, dst.top() + db, dw - 2 * db, dh - 2 * db), img,
                     QRect(sb, sb, w - 2 * sb, h - 2 * sb));
}

void CalendarRenderer::drawHeaderIcon(QPainter *g, HeaderButtonKind kind, const QRect &r)
{
    QPen pen(QColor(244, 244, 244, 255), 1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    g->setPen(pen);
    g->setBrush(Qt::NoBrush);
    const qreal cx = r.left() + r.width() / 2.0, cy = r.top() + r.height() / 2.0;
    switch (kind) {
        case HeaderButtonKind::Previous:
            g->drawPolyline(QVector<QPointF>{ QPointF(cx + 3, cy - 6), QPointF(cx - 3, cy),
                                              QPointF(cx + 3, cy + 6) });
            break;
        case HeaderButtonKind::Next:
            g->drawPolyline(QVector<QPointF>{ QPointF(cx - 3, cy - 6), QPointF(cx + 3, cy),
                                              QPointF(cx - 3, cy + 6) });
            break;
        case HeaderButtonKind::Adjust: // 移动十字箭头
            g->drawLine(QPointF(cx - 7, cy), QPointF(cx + 7, cy));
            g->drawLine(QPointF(cx, cy - 7), QPointF(cx, cy + 7));
            g->drawPolyline(QVector<QPointF>{ QPointF(cx - 4, cy - 3), QPointF(cx - 7, cy),
                                              QPointF(cx - 4, cy + 3) });
            g->drawPolyline(QVector<QPointF>{ QPointF(cx + 4, cy - 3), QPointF(cx + 7, cy),
                                              QPointF(cx + 4, cy + 3) });
            g->drawPolyline(QVector<QPointF>{ QPointF(cx - 3, cy - 4), QPointF(cx, cy - 7),
                                              QPointF(cx + 3, cy - 4) });
            g->drawPolyline(QVector<QPointF>{ QPointF(cx - 3, cy + 4), QPointF(cx, cy + 7),
                                              QPointF(cx + 3, cy + 4) });
            break;
    }
}

} // namespace poptools
