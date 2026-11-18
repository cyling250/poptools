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
    QRect monthLink;                          // 头部左上角月报链接热区（T-067；有月报才绘制）
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
    // cellDate = firstCellDate + row*7+col；首尾行列的日期可能越出显示月
    QDate cellDate(int row, int col) const;
    // 格内摘要链接热区（T-067）：日期行下方右对齐固定条带，renderer 绘制与
    // 命中测试共用同一几何（固定像素，不随 fontScale——对齐 headerButtons 先例）
    QRect cellSummaryLinkRect(const QRect &cell) const;
    // 命中格内摘要链接：p 在某格的链接条带内返回 true 并带回该格日期
    //（可能越出显示月，调用方自查）
    bool hitCellSummaryLink(const QPoint &p, QDate *date) const;
    // 命中测试：p∈gridContent 返回 true，行列 clamp 到 [0,rows)×[0,7]；返回的
    // date 可能越出显示月（首尾行），调用方必须自查 inDisplayMonth
    bool tryGetCell(const QPoint &p, int *row, int *col, QDate *date, QRect *rect) const;
    // 命中头部按钮返回下标（语义对应 headerKinds），无命中 -1
    int hitHeader(const QPoint &p) const;
    bool inDisplayMonth(const QDate &d) const { return d.year() == year && d.month() == month; }
};

} // namespace poptools
