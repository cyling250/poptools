#include "LunarCalendar.h"

#include <array>

namespace poptools {

// ---- 农历压缩数据表（1900-2100，社区通行的经典表） ----
// 每年一个整数：低 4 位闰月月份；bit16 闰月天数（1=30天）；
// bit(16-m)（m=1..12）为第 m 月是否大月（30 天）。
static constexpr std::array<unsigned int, 201> kLunarInfo = {
    0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0, 0x09ad0, 0x055d2, // 1900-1909
    0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2, 0x095b0, 0x14977, // 1910-1919
    0x04970, 0x0a4b0, 0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970, // 1920-1929
    0x06566, 0x0d4a0, 0x0ea50, 0x16a95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950, // 1930-1939
    0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2, 0x0a950, 0x0b557, // 1940-1949
    0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5b0, 0x14573, 0x052b0, 0x0a9a8, 0x0e950, 0x06aa0, // 1950-1959
    0x0aea6, 0x0ab50, 0x04b60, 0x0aae4, 0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0, // 1960-1969
    0x096d0, 0x04dd5, 0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b6a0, 0x195a6, // 1970-1979
    0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46, 0x0ab60, 0x09570, // 1980-1989
    0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58, 0x05ac0, 0x0ab60, 0x096d5, 0x092e0, // 1990-1999
    0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5, // 2000-2009
    0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930, // 2010-2019
    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530, // 2020-2029
    0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0, 0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45, // 2030-2039
    0x0b5a0, 0x056d0, 0x055b2, 0x049b0, 0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0, // 2040-2049
    0x14b63, 0x09370, 0x049f8, 0x04970, 0x064b0, 0x168a6, 0x0ea50, 0x06aa0, 0x1a6c4, 0x0aae0, // 2050-2059
    0x092e0, 0x0d2e3, 0x0c960, 0x0d557, 0x0d4a0, 0x0da50, 0x05d55, 0x056a0, 0x0a6d0, 0x055d4, // 2060-2069
    0x052d0, 0x0a9b8, 0x0a950, 0x0b4a0, 0x0b6a6, 0x0ad50, 0x055a0, 0x0aba4, 0x0a5b0, 0x052b0, // 2070-2079
    0x0b273, 0x06930, 0x07337, 0x06aa0, 0x0ad50, 0x14b55, 0x04b60, 0x0a570, 0x054e4, 0x0d160, // 2080-2089
    0x0e968, 0x0d520, 0x0daa0, 0x16aa6, 0x056d0, 0x04ae0, 0x0a9d4, 0x0a2d0, 0x0d150, 0x0f252, // 2090-2099
    0x0d520, // 2100
};

static const QDate kTableBegin(1900, 1, 31); // 1900 年正月初一
static const QDate kTableEnd(2101, 1, 28);   // 2100 年腊月廿九（表内最后一天）

// 第 year 农历年闰月月份（0 = 无闰月）
static int lunarLeapMonth(int year)
{
    return kLunarInfo[year - 1900] & 0xF;
}

// 第 year 农历年闰月天数（无闰月返回 0）
static int lunarLeapDays(int year)
{
    if (lunarLeapMonth(year) == 0)
        return 0;
    return (kLunarInfo[year - 1900] & 0x10000) ? 30 : 29;
}

// 第 year 农历年第 month 个（1-12）常规月的天数
static int lunarMonthDays(int year, int month)
{
    return (kLunarInfo[year - 1900] & (0x10000 >> month)) ? 30 : 29;
}

// 第 year 农历年总天数
static int lunarYearDays(int year)
{
    int sum = 348; // 12 个月各 29 天的基数
    // 大月位：bit15(正月) .. bit4(十二月)——注意 0x10 也是月份位，不能漏
    for (unsigned int bit = 0x8000; bit >= 0x10; bit >>= 1)
        sum += (kLunarInfo[year - 1900] & bit) ? 1 : 0;
    return sum + lunarLeapDays(year);
}

// 农历月显示名（不含"闰"前缀）
static const char *kMonthNames[] = {
    "", "正月", "二月", "三月", "四月", "五月", "六月",
    "七月", "八月", "九月", "十月", "冬月", "腊月",
};

// 农历日显示名
static const char *kDayNames[] = {
    "", "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
    "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
    "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
};

QString LunarDate::monthName() const
{
    if (!isValid())
        return QString();
    QString name = QString::fromUtf8(kMonthNames[month]);
    if (leap)
        name.prepend(QStringLiteral("闰"));
    return name;
}

QString LunarDate::dayName() const
{
    if (!isValid() || day > 30)
        return QString();
    return QString::fromUtf8(kDayNames[day]);
}

LunarDate lunar::fromSolar(const QDate &date)
{
    LunarDate result;
    if (!date.isValid() || date < kTableBegin || date > kTableEnd)
        return result;

    // 相对 1900 年正月初一的天数，逐年扣减定位农历年
    int offset = static_cast<int>(kTableBegin.daysTo(date));
    int year = 1900;
    while (year < 2100 && offset >= lunarYearDays(year)) {
        offset -= lunarYearDays(year);
        ++year;
    }

    // 逐月消耗剩余天数：常规月 m 之后紧跟闰 m 月（若该年有闰月在 m 位）
    result.year = year;
    int daysLeft = offset;
    for (int m = 1; m <= 12; ++m) {
        const int md = lunarMonthDays(year, m);
        if (daysLeft < md) {
            result.month = m;
            result.day = daysLeft + 1;
            result.leap = false;
            return result;
        }
        daysLeft -= md;

        if (lunarLeapMonth(year) != 0 && m == lunarLeapMonth(year)) {
            const int ld = lunarLeapDays(year);
            if (daysLeft < ld) {
                result.month = m;
                result.day = daysLeft + 1;
                result.leap = true;
                return result;
            }
            daysLeft -= ld;
        }
    }
    return LunarDate(); // 防御：表数据异常时返回无效值（正常不达此处）
}

QString lunar::dayLabel(const QDate &date, bool *isFestival)
{
    if (isFestival)
        *isFestival = false;
    if (!date.isValid())
        return QString();

    // 公历节日（优先级低，先查农历节日）
    struct SolarFestival { int month, day; const char *name; };
    static constexpr SolarFestival kSolarFestivals[] = {
        { 1, 1, "元旦" }, { 5, 1, "劳动节" }, { 10, 1, "国庆节" },
    };

    const LunarDate lunarDate = fromSolar(date);
    if (!lunarDate.isValid())
        return QString();

    // 农历节日（除夕单独判定：腊月最后一天）
    QString festival;
    if (lunarDate.month == 1 && lunarDate.day == 1)
        festival = QStringLiteral("春节");
    else if (lunarDate.month == 1 && lunarDate.day == 15)
        festival = QStringLiteral("元宵节");
    else if (lunarDate.month == 5 && lunarDate.day == 5)
        festival = QStringLiteral("端午节");
    else if (lunarDate.month == 7 && lunarDate.day == 7)
        festival = QStringLiteral("七夕");
    else if (lunarDate.month == 8 && lunarDate.day == 15)
        festival = QStringLiteral("中秋节");
    else if (lunarDate.month == 9 && lunarDate.day == 9)
        festival = QStringLiteral("重阳节");
    else if (lunarDate.month == 12 && lunarDate.day == 8)
        festival = QStringLiteral("腊八节");
    else if (lunarDate.month == 12 && !lunarDate.leap) {
        // 腊月最后一天：明天就是下一个正月初一
        const LunarDate next = fromSolar(date.addDays(1));
        if (next.isValid() && next.year == lunarDate.year + 1 && next.month == 1 && next.day == 1)
            festival = QStringLiteral("除夕");
    }
    if (!festival.isEmpty()) {
        if (isFestival)
            *isFestival = true;
        return festival;
    }

    for (const auto &f : kSolarFestivals) {
        if (date.month() == f.month && date.day() == f.day) {
            if (isFestival)
                *isFestival = true;
            return QString::fromUtf8(f.name);
        }
    }

    // 初一显示月名（月历惯例，一眼看出农历月切换）；平日显示日名
    if (lunarDate.day == 1)
        return lunarDate.monthName();
    return lunarDate.dayName();
}

} // namespace poptools
