#pragma once

#include <QDate>
#include <QString>

namespace poptools {

enum class DayLabelKind
{
    Lunar,      // 农历日（初X/廿X）
    LunarMonth, // 农历月初一（"农历X月"）
    SolarTerm,  // 节气（绿）
    Holiday     // 节日（橙）
};

struct DayLabel
{
    QString text;
    DayLabelKind kind = DayLabelKind::Lunar;
};

// ============================================================
// 农历 / 节气 / 节日 / ISO 周数（照搬 rlqd CalendarClone/ChineseCalendar.cs；
// .NET ChineseLunisolarCalendar 等价实现：1900-2100 压缩表 + 迭代换算）。
// ============================================================
class ChineseCalendar
{
public:
    // 星期全称（Qt dayOfWeek：1=周一 … 7=周日）
    static QString weekdayName(const QDate &d);

    // 公历 → 农历（年/月/日；返回 false = 超出 1900-2100 支持范围）
    static bool getLunar(const QDate &d, int *year, int *month, int *day);
    // 该日是否恰处其农历年的闰月内（出表日期返回 false）
    static bool isLeapLunarMonth(const QDate &d);
    static QString lunarMonthName(const QDate &d); // "正月".."腊月"，闰月带"闰"前缀
    static QString lunarDayName(const QDate &d);   // "初一".."三十"

    // 节气（与 rlqd 同一分钟级近似公式；非节气日返回空串）
    static QString solarTerm(const QDate &d);

    // 节日（农历节日跳过闰月；感恩节 = 11 月第 4 个周四）
    static QString festival(const QDate &d);

    // 日历格标签：节气 > 节日 > 农历（showLunar=false 时不显示农历日）
    static DayLabel dayLabel(const QDate &d, bool showLunar = true);

    static int weekNumber(const QDate &d); // ISO 8601 周数

    // 公历月序 1-12 → "一月".."十二月"；越界回退 "%1月"
    static QString monthNameChinese(int month);

    // 标题：本月显示"2026年11月16日  星期一  农历十月初八"，跨月显示"2026年11月"
    static QString monthTitle(const QDate &today, int year, int month);
};

} // namespace poptools
