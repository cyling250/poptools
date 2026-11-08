#include "MonthView.h"

#include "core/calendar/LunarCalendar.h"

#include <QMouseEvent>
#include <QPainter>

namespace poptools {

MonthView::MonthView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(false);
    m_pal.bg = QColor("#FFFFFF");
    m_pal.text = QColor("#000000");
    m_pal.subText = QColor(0, 0, 0, 150);
    m_pal.dimText = QColor(0, 0, 0, 60);
    m_pal.festival = QColor(0xE0, 0x53, 0x3D);
    m_pal.accent = QColor(0x2B, 0x6F, 0xF2);
    m_pal.line = QColor(0, 0, 0, 25);

    m_selected = QDate::currentDate();
}

void MonthView::setSelectedDate(const QDate &date)
{
    if (!date.isValid() || date == m_selected)
        return;
    m_selected = date;
    update();
}

void MonthView::setEventDots(const QHash<QDate, QVector<QColor>> &dots)
{
    m_dots = dots;
    update();
}

void MonthView::setViewPalette(const Palette &p)
{
    m_pal = p;
    update();
}

int MonthView::firstDayColumn(const QDate &firstOfMonth)
{
    // 周一=0 … 周日=6（Qt dayOfWeek: Mon=1..Sun=7）
    return firstOfMonth.dayOfWeek() - 1;
}

QRect MonthView::cellRectFor(const QDate &date) const
{
    const QDate first(m_selected.year(), m_selected.month(), 1);
    if (!date.isValid() || date < first || date > first.addMonths(1).addDays(-1))
        return QRect();
    const int pos = firstDayColumn(first) + first.daysTo(date);
    return cellRect(pos / colCount(), pos % colCount());
}

QRect MonthView::cellRect(int row, int col) const
{
    const int headerH = 26; // 星期表头行高
    const int w = width() / colCount();
    const int h = (height() - headerH) / rowCount();
    return QRect(col * w, headerH + row * h, w, h);
}

QDate MonthView::dateAt(const QPoint &pos) const
{
    for (int row = 0; row < rowCount(); ++row) {
        for (int col = 0; col < colCount(); ++col) {
            if (cellRect(row, col).contains(pos)) {
                const QDate first(m_selected.year(), m_selected.month(), 1);
                const int offset = row * colCount() + col - firstDayColumn(first);
                return first.addDays(offset);
            }
        }
    }
    return QDate();
}

void MonthView::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.fillRect(rect(), m_pal.bg);

    // ---- 星期表头 ----
    painter.setFont(font());
    painter.setPen(m_pal.subText);
    static const char *kWeek[] = { "一", "二", "三", "四", "五", "六", "日" };
    for (int col = 0; col < colCount(); ++col) {
        const QRect r = cellRect(0, col);
        const bool weekend = col >= 5;
        painter.setPen(weekend ? m_pal.festival : m_pal.subText);
        painter.drawText(QRect(r.x(), 2, r.width(), 22), Qt::AlignCenter,
                         QString::fromUtf8(kWeek[col]));
    }

    // ---- 日期格子 ----
    const QDate first(m_selected.year(), m_selected.month(), 1);
    const int startCol = firstDayColumn(first);
    const QDate today = QDate::currentDate();
    QFont solarFont = font();
    solarFont.setPixelSize(qBound(13, height() / 22, 18));
    QFont lunarFont = font();
    lunarFont.setPixelSize(qMax(9, solarFont.pixelSize() - 6));

    for (int row = 0; row < rowCount(); ++row) {
        for (int col = 0; col < colCount(); ++col) {
            const QRect cell = cellRect(row, col);
            const int offset = row * colCount() + col - startCol;
            const QDate day = first.addDays(offset);

            const bool currentMonth = day.month() == m_selected.month();
            const bool isToday = day == today;
            const bool isSelected = day == m_selected;

            // 选中/今天底色（选中 = 强调色实底；今天未选中 = 描边圈）
            if (isSelected) {
                const QRect r = cell.adjusted(3, 3, -3, -3);
                painter.setPen(Qt::NoPen);
                painter.setBrush(m_pal.accent);
                painter.drawRoundedRect(r, 7, 7);
            } else if (isToday) {
                const QRect r = cell.adjusted(3, 3, -3, -3);
                QPen pen(m_pal.accent);
                pen.setWidthF(1.4);
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(r, 7, 7);
            }

            // 公历数字（今天加粗）
            QColor numberColor = currentMonth ? m_pal.text : m_pal.dimText;
            if (isSelected)
                numberColor = QColor("#FFFFFF");
            solarFont.setBold(isToday || isSelected);
            painter.setFont(solarFont);
            painter.setPen(numberColor);
            painter.drawText(cell.adjusted(0, 4, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
                             QString::number(day.day()));

            // 农历小字（节日用节日色；选中格上反白）
            painter.setFont(lunarFont);
            bool isFestival = false;
            const QString label = lunar::dayLabel(day, &isFestival);
            if (!label.isEmpty()) {
                QColor lunarColor = isFestival && currentMonth
                                        ? m_pal.festival
                                        : (currentMonth ? m_pal.subText : m_pal.dimText);
                if (isSelected)
                    lunarColor = QColor(255, 255, 255, 220);
                painter.setPen(lunarColor);
                painter.drawText(cell.adjusted(0, 0, 0, -2), Qt::AlignHCenter | Qt::AlignBottom,
                                 label);
            }

            // 事项圆点（最多 3 个，居中排布在格子底部）
            const auto dotIt = m_dots.constFind(day);
            if (dotIt != m_dots.constEnd() && !dotIt->isEmpty()) {
                const int dotR = 2;
                const int n = qMin(3, dotIt->size());
                const int spacing = dotR * 2 + 3;
                const int totalW = n * spacing - 3;
                int x = cell.center().x() - totalW / 2 + dotR;
                const int y = cell.bottom() - 5;
                painter.setPen(Qt::NoPen);
                for (int i = 0; i < n; ++i) {
                    painter.setBrush(dotIt->at(i));
                    painter.drawEllipse(QPoint(x, y), dotR, dotR);
                    x += spacing;
                }
            }
        }
    }
}

void MonthView::mousePressEvent(QMouseEvent *event)
{
    const QDate day = dateAt(event->position().toPoint());
    if (day.isValid()) {
        if (day != m_selected) {
            m_selected = day;
            update();
            emit selectedDateChanged(day);
        }
    }
    QWidget::mousePressEvent(event);
}

void MonthView::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QDate day = dateAt(event->position().toPoint());
    if (day.isValid())
        emit dayActivated(day);
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace poptools
