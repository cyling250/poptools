#include "CalendarLayout.h"

#include <algorithm>

namespace poptools {

CalendarLayout::CalendarLayout(const QRect &panelRect, int year_, int month_)
    : panel(panelRect)
    , year(year_)
    , month(month_)
{
    titleBar = QRect(panel.left(), panel.top(), panel.width(), kTitleHeight);
    weekHeader = QRect(panel.left() + kWeekNumberWidth + kPad, panel.top() + kTitleHeight,
                       panel.width() - kWeekNumberWidth - kPad * 2, kWeekHeaderHeight);
    bottomBar = QRect(panel.left(), panel.bottom() - kBottomHeight + 1, panel.width(), kBottomHeight);
    gridArea = QRect(panel.left(), panel.top() + kTitleHeight, panel.width(),
                     panel.height() - kTitleHeight - kBottomHeight);
    gridContent = QRect(panel.left() + kWeekNumberWidth + kPad,
                        panel.top() + kTitleHeight + kWeekHeaderHeight,
                        panel.width() - kWeekNumberWidth - kPad * 2,
                        std::max(1, panel.height() - kTitleHeight - kWeekHeaderHeight
                                        - kBottomHeight));
    weekNumberArea = QRect(panel.left(), gridContent.top(), kWeekNumberWidth, gridContent.height());

    const QDate first(year, month, 1);
    const int offset = first.dayOfWeek() - 1; // Qt 周一=1 → 周一为第 0 列
    const bool skipPartial = (offset == 6);   // 周日开月的首周只有一天，跳过
    const QDate start = skipPartial ? first.addDays(1) : first.addDays(-offset);
    const int daysInMonth = QDate(year, month, 1).daysInMonth();
    const int shownDays = int(QDate(year, month, daysInMonth).toJulianDay() - start.toJulianDay()) + 1;
    rows = std::clamp(int(std::ceil(shownDays / 7.0)), 5, 6);
    firstCellDate = start;
    cellWidth = std::max(1, gridContent.width() / 7);
    cellHeight = std::max(1, gridContent.height() / rows);

    int bx = panel.right() - 12 - 26;
    headerButtons.resize(3);
    for (int i = 2; i >= 0; --i) {
        headerButtons[i] = QRect(bx, panel.top() + 12, 26, 26);
        bx -= 32;
    }

    nextMonthPill = QRect(panel.left() + 40, bottomBar.top() + 6, 92, 22);
    guidePill = QRect(panel.right() - 236, panel.top() + 34, 224, 34);
}

QRect CalendarLayout::cellRect(int row, int col) const
{
    return QRect(gridContent.left() + col * cellWidth, gridContent.top() + row * cellHeight,
                 cellWidth, cellHeight);
}

QDate CalendarLayout::cellDate(int row, int col) const
{
    return firstCellDate.addDays(row * 7 + col);
}

bool CalendarLayout::tryGetCell(const QPoint &p, int *row, int *col, QDate *date, QRect *rect) const
{
    if (row) *row = 0;
    if (col) *col = 0;
    if (date) *date = firstCellDate;
    if (rect) *rect = QRect();
    if (!gridContent.contains(p))
        return false;
    const int c = std::clamp((p.x() - gridContent.left()) / cellWidth, 0, 6);
    const int r = std::clamp((p.y() - gridContent.top()) / cellHeight, 0, rows - 1);
    if (col) *col = c;
    if (row) *row = r;
    if (date) *date = cellDate(r, c);
    if (rect) *rect = cellRect(r, c);
    return true;
}

int CalendarLayout::hitHeader(const QPoint &p) const
{
    for (int i = 0; i < headerButtons.size(); ++i)
        if (headerButtons.at(i).contains(p))
            return i;
    return -1;
}

} // namespace poptools
