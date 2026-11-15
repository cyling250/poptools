#include "CalendarRenderer.h"

#include "CalendarSharedCanvas.h"
#include "core/calendar/ChineseCalendar.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>

#include <cmath>

namespace poptools {

namespace {

QPainterPath roundedPath(const QRectF &r, qreal radius)
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

} // namespace

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

CalendarLayout CalendarRenderer::render(QPainter *g, CalendarSharedCanvas &canvas,
                                        const CalendarSettings &settings,
                                        const CalendarTaskStore &store, const QDate &today,
                                        int year, int month, const QPoint &hover,
                                        const QString &hoverHint, bool adjusting)
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
        canvas.setCalendarRect(panel);
        canvas.markReady();
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
                drawNineSlice(g, bg, cell, 12);
            if (!inMonth)
                continue;

            const QFontMetrics fm(dateFont);
            const qreal dateTextW = fm.horizontalAdvance(QString::number(date.day()));
            const QPointF datePos(cell.left() + 8, cell.top() + 3);
            drawShadowText(g, QString::number(date.day()), dateFont,
                           QColor(246, 246, 246, 255), datePos);
            if (!label.text.isEmpty()) {
                QColor lc(188, 188, 188, 255);
                switch (label.kind) {
                    case DayLabelKind::SolarTerm: lc = QColor(112, 214, 138, 255); break;
                    case DayLabelKind::Holiday: lc = QColor(240, 166, 72, 255); break;
                    case DayLabelKind::LunarMonth: lc = QColor(214, 214, 214, 255); break;
                    default: lc = QColor(188, 188, 188, 255); break;
                }
                drawShadowText(g, label.text, lunarFont, lc,
                               datePos + QPointF(dateTextW + 6, 3));
            }

            if (settings.showEvents) {
                const QVector<CalendarTaskItem> tasks = store.get(date);
                if (!tasks.isEmpty()) {
                    const qreal lineH = 17 * scale;
                    g->save();
                    g->setClipRect(QRect(cell.left(), cell.top(), cell.width(), cell.height() - 2));
                    qreal ey = cell.top() + 27;
                    for (int i = 0; i < tasks.size(); ++i) {
                        if (ey + lineH > cell.bottom())
                            break;
                        const CalendarTaskItem &t = tasks.at(i);
                        const QString txt =
                            t.time.isEmpty()
                                ? QStringLiteral("%1. %2").arg(i + 1).arg(t.text)
                                : QStringLiteral("%1. %2 %3").arg(i + 1).arg(t.time, t.text);
                        QFont f = eventFont;
                        if (t.done) {
                            f.setStrikeOut(true);
                        }
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
                if (!m_todayGlow.isNull())
                    drawNineSliceBorder(g, m_todayGlow, cell, 20, 14);
                QPen hp(QColor(255, 255, 244, 215), 1.1);
                g->setPen(hp);
                g->setBrush(Qt::NoBrush);
                g->drawRect(QRectF(cell));
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

    // 星期表头
    {
        const QFontMetrics wfm(weekHeaderFont);
        for (int c = 0; c < 7; ++c) {
            const QDate day(2020, 1, 6 + c); // 2020-01-06 = 周一 … 01-12 = 周日
            const QString text = ChineseCalendar::weekdayShort(day);
            const qreal w = wfm.horizontalAdvance(text);
            drawShadowText(g, text, weekHeaderFont, QColor(226, 226, 226, 255),
                           QPointF(layout.gridContent.left() + c * layout.cellWidth
                                       + (layout.cellWidth - w) / 2.0,
                                   layout.weekHeader.top() + 3));
        }
    }

    // 周数 + 当前周金色条
    {
        const int currentWeek = ChineseCalendar::weekNumber(today);
        const QFontMetrics nfm(weekFont);
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
        const QFontMetrics tfm(titleFont);
        const qreal w = tfm.horizontalAdvance(title);
        const qreal tx = panel.left() + (panel.width() - w) / 2.0;
        const qreal ty = panel.top() + (layout.kTitleHeight - tfm.height()) / 2.0;
        drawShadowText(g, title, titleFont, QColor(250, 250, 250, 255), QPointF(tx, ty));
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
            const QFontMetrics pfm(pillFont);
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

    canvas.setCalendarRect(panel);
    canvas.markReady();
    return layout;
}

void CalendarRenderer::drawShadow(QPainter *g, const QRect &panel)
{
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
    QFontMetrics fm(f);
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
        g->setPen(QColor(0, 0, 0, 120));
        g->drawText(pos + QPointF(1, 1), text);
        g->setPen(color);
        g->drawText(pos, text);
    }
}

void CalendarRenderer::drawNineSlice(QPainter *g, const QImage &img, const QRect &dst, int border)
{
    const int w = img.width(), h = img.height();
    const int b = std::min(border, std::min(w / 2, h / 2));
    const int dw = dst.width(), dh = dst.height();
    g->drawImage(QRect(dst.left(), dst.top(), b, b), img, QRect(0, 0, b, b));
    g->drawImage(QRect(dst.right() - b + 1, dst.top(), b, b), img, QRect(w - b, 0, b, b));
    g->drawImage(QRect(dst.left(), dst.bottom() - b + 1, b, b), img, QRect(0, h - b, b, b));
    g->drawImage(QRect(dst.right() - b + 1, dst.bottom() - b + 1, b, b), img,
                 QRect(w - b, h - b, b, b));
    g->drawImage(QRect(dst.left() + b, dst.top(), dw - 2 * b, b), img, QRect(b, 0, w - 2 * b, b));
    g->drawImage(QRect(dst.left() + b, dst.bottom() - b + 1, dw - 2 * b, b), img,
                 QRect(b, h - b, w - 2 * b, b));
    g->drawImage(QRect(dst.left(), dst.top() + b, b, dh - 2 * b), img, QRect(0, b, b, h - 2 * b));
    g->drawImage(QRect(dst.right() - b + 1, dst.top() + b, b, dh - 2 * b), img,
                 QRect(w - b, b, b, h - 2 * b));
    g->drawImage(QRect(dst.left() + b, dst.top() + b, dw - 2 * b, dh - 2 * b), img,
                 QRect(b, b, w - 2 * b, h - 2 * b));
}

void CalendarRenderer::drawNineSliceBorder(QPainter *g, const QImage &img, const QRect &dst,
                                           int srcBorder, int destBorder)
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
        case HeaderButtonKind::Settings: {
            g->drawEllipse(QPointF(cx, cy), 4, 4);
            for (int i = 0; i < 8; ++i) {
                const double a = i * M_PI / 4;
                g->drawLine(QPointF(cx + std::cos(a) * 6, cy + std::sin(a) * 6),
                            QPointF(cx + std::cos(a) * 8.5, cy + std::sin(a) * 8.5));
            }
            break;
        }
        case HeaderButtonKind::Plugin:
            g->drawRect(QRectF(cx - 6.5, cy - 5, 13, 9));
            g->drawLine(QPointF(cx, cy + 4), QPointF(cx, cy + 7));
            g->drawLine(QPointF(cx - 3, cy + 7), QPointF(cx + 3, cy + 7));
            break;
        case HeaderButtonKind::Cloud: {
            QPainterPath path;
            path.arcMoveTo(QRectF(cx - 8, cy - 2, 9, 9), 180);
            path.arcTo(QRectF(cx - 8, cy - 2, 9, 9), 180, 180);
            path.arcTo(QRectF(cx - 1, cy - 8, 10, 10), 200, 160);
            path.arcTo(QRectF(cx + 1, cy - 2, 8, 8), 270, 170);
            path.lineTo(cx + 8, cy + 6);
            path.lineTo(cx - 7, cy + 6);
            g->drawPath(path);
            break;
        }
        case HeaderButtonKind::Account: {
            g->drawEllipse(QPointF(cx, cy - 3.8), 3.2, 3.2);
            QPainterPath path;
            path.arcMoveTo(QRectF(cx - 7, cy + 1, 14, 12), 180);
            path.arcTo(QRectF(cx - 7, cy + 1, 14, 12), 180, 180);
            g->drawPath(path);
            break;
        }
        case HeaderButtonKind::Previous:
            g->drawPolyline(QVector<QPointF>{ QPointF(cx + 3, cy - 6), QPointF(cx - 3, cy),
                                              QPointF(cx + 3, cy + 6) });
            break;
        case HeaderButtonKind::Next:
            g->drawPolyline(QVector<QPointF>{ QPointF(cx - 3, cy - 6), QPointF(cx + 3, cy),
                                              QPointF(cx - 3, cy + 6) });
            break;
        case HeaderButtonKind::Menu:
            g->drawLine(QPointF(cx - 7, cy - 5), QPointF(cx + 7, cy - 5));
            g->drawLine(QPointF(cx - 7, cy), QPointF(cx + 7, cy));
            g->drawLine(QPointF(cx - 7, cy + 5), QPointF(cx + 7, cy + 5));
            break;
    }
}

QPainterPath CalendarRenderer::roundRectPath(const QRectF &r, qreal radius)
{
    return roundedPath(r, radius);
}

} // namespace poptools
