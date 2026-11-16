#pragma once

#include <QDate>
#include <QRect>
#include <QVector>

namespace poptools {

enum class HeaderButtonKind
{
    Previous,
    Next,
    Adjust
};

// ============================================================
// 日历布局与命中测试（照搬 rlqd CalendarClone/CalendarLayout.cs）。
// 坐标全部为共享画布上的物理像素。
// ============================================================
class CalendarLayout
{
public:
    QRect panel;
    QRect titleBar;
    QRect weekHeader;
    QRect gridArea;
    QRect gridContent;
    QRect bottomBar;
    QRect weekNumberArea;
    QRect nextMonthPill;
    QRect guidePill;
    QVector<QRect> headerButtons;             // 3 个，右起往左（翻页 + 调整，其余入口在应用设置页）
    static constexpr HeaderButtonKind headerKinds[3] = {
        HeaderButtonKind::Previous, HeaderButtonKind::Next, HeaderButtonKind::Adjust
    };

    int year = 0;
    int month = 0;
    int rows = 5;
    int cellWidth = 1;
    int cellHeight = 1;
    QDate firstCellDate;
    static constexpr int kTitleHeight = 50;
    static constexpr int kWeekHeaderHeight = 22;
    static constexpr int kWeekNumberWidth = 48;
    static constexpr int kBottomHeight = 34;
    static constexpr int kPad = 4;

    CalendarLayout() = default;
    CalendarLayout(const QRect &panelRect, int year, int month);

    QRect cellRect(int row, int col) const;
    QDate cellDate(int row, int col) const;
    bool tryGetCell(const QPoint &p, int *row, int *col, QDate *date, QRect *rect) const;
    int hitHeader(const QPoint &p) const;
    bool inDisplayMonth(const QDate &d) const { return d.year() == year && d.month() == month; }
};

} // namespace poptools
