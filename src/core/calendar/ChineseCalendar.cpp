#include "ChineseCalendar.h"

#include <QDateTime>

namespace poptools {

namespace {

// 1900-2100 农历压缩表（bit12~1 = 十二个月大小 30/29，bit16 = 闰月大小，
// 低 4 位 = 闰月月份；与权威实现逐项一致，2020-2030 逐日真值校验通过）
constexpr quint32 kLunarInfo[201] = {
    0x04bd8,0x04ae0,0x0a570,0x054d5,0x0d260,0x0d950,0x16554,0x056a0,0x09ad0,0x055d2, //1900-1909
    0x04ae0,0x0a5b6,0x0a4d0,0x0d250,0x1d255,0x0b540,0x0d6a0,0x0ada2,0x095b0,0x14977, //1910-1919
    0x04970,0x0a4b0,0x0b4b5,0x06a50,0x06d40,0x1ab54,0x02b60,0x09570,0x052f2,0x04970, //1920-1929
    0x06566,0x0d4a0,0x0ea50,0x06e95,0x05ad0,0x02b60,0x186e3,0x092e0,0x1c8d7,0x0c950, //1930-1939
    0x0d4a0,0x1d8a6,0x0b550,0x056a0,0x1a5b4,0x025d0,0x092d0,0x0d2b2,0x0a950,0x0b557, //1940-1949
    0x06ca0,0x0b550,0x15355,0x04da0,0x0a5b0,0x14573,0x052b0,0x0a9a8,0x0e950,0x06aa0, //1950-1959
    0x0aea6,0x0ab50,0x04b60,0x0aae4,0x0a570,0x05260,0x0f263,0x0d950,0x05b57,0x056a0, //1960-1969
    0x096d0,0x04dd5,0x04ad0,0x0a4d0,0x0d4d4,0x0d250,0x0d558,0x0b540,0x0b6a0,0x195a6, //1970-1979
    0x095b0,0x049b0,0x0a974,0x0a4b0,0x0b27a,0x06a50,0x06d40,0x0af46,0x0ab60,0x09570, //1980-1989
    0x04af5,0x04970,0x064b0,0x074a3,0x0ea50,0x06b58,0x05ac0,0x0ab60,0x096d5,0x092e0, //1990-1999
    0x0c960,0x0d954,0x0d4a0,0x0da50,0x07552,0x056a0,0x0abb7,0x025d0,0x092d0,0x0cab5, //2000-2009
    0x0a950,0x0b4a0,0x0baa4,0x0ad50,0x055d9,0x04ba0,0x0a5b0,0x15176,0x052b0,0x0a930, //2010-2019
    0x07954,0x06aa0,0x0ad50,0x05b52,0x04b60,0x0a6e6,0x0a4e0,0x0d260,0x0ea65,0x0d530, //2020-2029
    0x05aa0,0x076a3,0x096d0,0x04afb,0x04ad0,0x0a4d0,0x1d0b6,0x0d250,0x0d520,0x0dd45, //2030-2039
    0x0b5a0,0x056d0,0x055b2,0x049b0,0x0a577,0x0a4b0,0x0aa50,0x1b255,0x06d20,0x0ada0, //2040-2049
    0x14b63,0x09370,0x049f8,0x04970,0x064b0,0x168a6,0x0ea50,0x06b20,0x1a6c4,0x0aae0, //2050-2059
    0x092e0,0x0d2e3,0x0c960,0x0d557,0x0d4a0,0x0da50,0x05d55,0x056a0,0x0a6d0,0x055d4, //2060-2069
    0x052d0,0x0a9b8,0x0a950,0x0b4a0,0x0b6a6,0x0ad50,0x055a0,0x0aba4,0x0a5b0,0x052b0, //2070-2079
    0x0b273,0x06930,0x07337,0x06aa0,0x0ad50,0x14b55,0x04b60,0x0a570,0x054e4,0x0d160, //2080-2089
    0x0e968,0x0d520,0x0daa0,0x16aa6,0x056d0,0x04ae0,0x0a9d4,0x0a4d0,0x0d150,0x0f252, //2090-2099
    0x0d520 //2100-2100
};

constexpr const char *kDayNames[] = {
    "初一","初二","初三","初四","初五","初六","初七","初八","初九","初十",
    "十一","十二","十三","十四","十五","十六","十七","十八","十九","二十",
    "廿一","廿二","廿三","廿四","廿五","廿六","廿七","廿八","廿九","三十"
};
constexpr const char *kMonthNames[] = {
    "正月","二月","三月","四月","五月","六月","七月","八月","九月","十月","冬月","腊月"
};
constexpr const char *kSolarTermNames[] = {
    "小寒","大寒","立春","雨水","惊蛰","春分","清明","谷雨","立夏","小满","芒种","夏至",
    "小暑","大暑","立秋","处暑","白露","秋分","寒露","霜降","立冬","小雪","大雪","冬至"
};
// 节气分钟数（与 rlqd 逐项相同：1900-01-06 02:05 起算的近似公式）
constexpr int kSolarTermInfo[24] = {
    0, 21208, 42467, 63836, 85337, 107014, 128867, 150921, 173149, 195551, 218072,
    240693, 263343, 285989, 308563, 331033, 353350, 375494, 397447, 419210, 440795,
    462224, 483532, 504758
};

int lunarLeapMonth(int y) { return kLunarInfo[y - 1900] & 0xf; }

int lunarLeapDaysRaw(int y)
{
    if (!lunarLeapMonth(y))
        return 0;
    return (kLunarInfo[y - 1900] & 0x10000) ? 30 : 29;
}

int lunarYearDays(int y)
{
    const quint32 info = kLunarInfo[y - 1900];
    int sum = 348;
    // bit 从 0x8000 走到 0x10（含）共 12 位——十二个月大小位一个都不能漏；
    // 表必须 quint32：闰八~闰腊月的年份值 ≥ 0x10000（bit16=闰月大小），quint16 会截断
    for (quint32 bit = 0x8000; bit > 0x8; bit >>= 1)
        sum += (info & bit) ? 1 : 0;
    return sum + lunarLeapDaysRaw(y);
}

int lunarMonthDays(int y, int m) { return (kLunarInfo[y - 1900] & (0x10000 >> m)) ? 30 : 29; }

} // namespace

QString ChineseCalendar::weekdayName(const QDate &d)
{
    switch (d.dayOfWeek()) { // Qt：1=周一 … 7=周日
        case 1: return QStringLiteral("星期一");
        case 2: return QStringLiteral("星期二");
        case 3: return QStringLiteral("星期三");
        case 4: return QStringLiteral("星期四");
        case 5: return QStringLiteral("星期五");
        case 6: return QStringLiteral("星期六");
        default: return QStringLiteral("星期日");
    }
}

QString ChineseCalendar::weekdayShort(const QDate &d) { return weekdayName(d); }

bool ChineseCalendar::getLunar(const QDate &d, int *year, int *month, int *day)
{
    if (year) *year = 0;
    if (month) *month = 0;
    if (day) *day = 0;
    if (!d.isValid())
        return false;
    const QDate base(1900, 1, 31); // 农历 1900 年正月初一
    if (d < base || d.year() > 2100)
        return false;
    int offset = base.daysTo(d);
    int y = 1900;
    int temp = 0;
    for (; y < 2101 && offset > 0; ++y) {
        temp = lunarYearDays(y);
        offset -= temp;
    }
    if (offset < 0) {
        offset += temp;
        --y;
    }
    const int leap = lunarLeapMonth(y);
    bool isLeap = false;
    int m = 1;
    temp = 0;
    for (; m < 13 && offset > 0; ++m) {
        if (leap > 0 && m == leap + 1 && !isLeap) {
            --m;
            isLeap = true;
            temp = lunarLeapDaysRaw(y);
        } else {
            temp = lunarMonthDays(y, m);
        }
        if (isLeap && m == leap + 1)
            isLeap = false;
        offset -= temp;
    }
    if (offset == 0 && leap > 0 && m == leap + 1) {
        if (isLeap) {
            isLeap = false;
        } else {
            isLeap = true;
            --m;
        }
    }
    if (offset < 0) {
        offset += temp;
        --m;
    }
    if (year) *year = y;
    if (month) *month = m;
    if (day) *day = offset + 1;
    return true;
}

bool ChineseCalendar::isLeapLunarMonth(const QDate &d)
{
    int y = 0, m = 0, day = 0;
    if (!getLunar(d, &y, &m, &day))
        return false;
    return lunarLeapMonth(y) == m;
}

QString ChineseCalendar::lunarMonthName(const QDate &d)
{
    int y = 0, m = 0, day = 0;
    if (!getLunar(d, &y, &m, &day))
        return {};
    const bool leap = lunarLeapMonth(y) == m;
    int normal = m;
    if (leap) {
        // 闰月本身：按月序取名字（闰X月）
    } else if (int leapM = lunarLeapMonth(y); leapM > 0 && m > leapM) {
        --normal; // 闰月之后的标准月序减一
    }
    normal = qBound(1, normal, 12);
    return (leap ? QStringLiteral("闰") : QString()) + QString::fromUtf8(kMonthNames[normal - 1]);
}

QString ChineseCalendar::lunarDayName(const QDate &d)
{
    int y = 0, m = 0, day = 0;
    if (!getLunar(d, &y, &m, &day))
        return {};
    return (day >= 1 && day <= 30) ? QString::fromUtf8(kDayNames[day - 1]) : QString();
}

QString ChineseCalendar::solarTerm(const QDate &d)
{
    const int idxBase = (d.month() - 1) * 2;
    for (int k = 0; k < 2; ++k) {
        const int idx = idxBase + k;
        if (idx < 0 || idx >= 24)
            continue;
        // 与 rlqd 相同：1900-01-06 02:05 + 每年 365.2422 天的分钟级近似
        const double ms = 31556925974.7 * (d.year() - 1900) + kSolarTermInfo[idx] * 60000.0;
        const QDateTime base(QDate(1900, 1, 6), QTime(2, 5, 0), Qt::LocalTime);
        const QDateTime term = base.addMSecs(qRound64(ms));
        const QDate td = term.date();
        if (td.month() == d.month() && td.day() == d.day())
            return QString::fromUtf8(kSolarTermNames[idx]);
    }
    return {};
}

namespace {

QDate nthWeekdayOfMonth(int year, int month, Qt::DayOfWeek dow, int n)
{
    const QDate first(year, month, 1);
    int offset = (int(dow) - int(first.dayOfWeek()) + 7) % 7;
    const QDate date = first.addDays(offset + (n - 1) * 7);
    return date.month() == month ? date : QDate();
}

} // namespace

QString ChineseCalendar::festival(const QDate &d)
{
    if (!d.isValid())
        return {};
    const int y = d.year(), m = d.month(), day = d.day();
    int ly = 0, lm = 0, ld = 0;
    if (!getLunar(d, &ly, &lm, &ld))
        return {};
    const bool leap = lunarLeapMonth(ly) == lm;

    if (!leap && lm == 1 && ld == 1) return QStringLiteral("春节");
    if (!leap && lm == 1 && ld == 15) return QStringLiteral("元宵节");
    if (!leap && lm == 5 && ld == 5) return QStringLiteral("端午节");
    if (!leap && lm == 8 && ld == 15) return QStringLiteral("中秋节");
    if (!leap && lm == 9 && ld == 9) return QStringLiteral("重阳节");
    if (!leap && lm == 12 && ld == 8) return QStringLiteral("腊八节");
    if (lm == 12 && ld >= 29 && !leap) {
        // 除夕：次日为农历正月初一
        int ny = 0, nm = 0, nd = 0;
        if (getLunar(d.addDays(1), &ny, &nm, &nd) && nm == 1 && nd == 1)
            return QStringLiteral("除夕");
    }
    if (m == 1 && day == 1) return QStringLiteral("元旦");
    if (m == 2 && day == 14) return QStringLiteral("情人节");
    if (m == 3 && day == 8) return QStringLiteral("妇女节");
    if (m == 4 && day == 1) return QStringLiteral("愚人节");
    if (m == 5 && day == 1) return QStringLiteral("劳动节");
    if (m == 6 && day == 1) return QStringLiteral("儿童节");
    if (m == 9 && day == 10) return QStringLiteral("教师节");
    if (m == 10 && day == 1) return QStringLiteral("国庆节");
    if (m == 10 && day == 31) return QStringLiteral("万圣节");
    if (m == 12 && day == 24) return QStringLiteral("平安夜");
    if (m == 12 && day == 25) return QStringLiteral("圣诞夜");
    if (m == 11) {
        const QDate thanksgiving = nthWeekdayOfMonth(y, 11, Qt::Thursday, 4);
        if (thanksgiving.isValid() && thanksgiving.day() == day)
            return QStringLiteral("感恩节");
    }
    return {};
}

DayLabel ChineseCalendar::dayLabel(const QDate &d, bool showLunar)
{
    const QString term = solarTerm(d);
    if (!term.isEmpty())
        return { term, DayLabelKind::SolarTerm };
    const QString fest = festival(d);
    if (!fest.isEmpty())
        return { fest, DayLabelKind::Holiday };
    if (!showLunar)
        return { {}, DayLabelKind::Lunar };
    int ly = 0, lm = 0, ld = 0;
    if (!getLunar(d, &ly, &lm, &ld))
        return { {}, DayLabelKind::Lunar };
    if (ld == 1)
        return { QStringLiteral("农历") + lunarMonthName(d), DayLabelKind::LunarMonth };
    return { lunarDayName(d), DayLabelKind::Lunar };
}

int ChineseCalendar::weekNumber(const QDate &d)
{
    // ISO 8601：含 1 月 4 日的一周为第 1 周；周四所在年 = ISO 年
    if (!d.isValid())
        return 0;
    const QDate thursday = d.addDays(4 - d.dayOfWeek()); // 周四 = dayOfWeek 4
    const int weekYear = thursday.year();
    QDate firstThursday(weekYear, 1, 4);
    firstThursday = firstThursday.addDays(4 - firstThursday.dayOfWeek());
    return (thursday.dayOfYear() - firstThursday.dayOfYear()) / 7 + 1;
}

QString ChineseCalendar::monthNameChinese(int month)
{
    switch (month) {
        case 1: return QStringLiteral("一月");
        case 2: return QStringLiteral("二月");
        case 3: return QStringLiteral("三月");
        case 4: return QStringLiteral("四月");
        case 5: return QStringLiteral("五月");
        case 6: return QStringLiteral("六月");
        case 7: return QStringLiteral("七月");
        case 8: return QStringLiteral("八月");
        case 9: return QStringLiteral("九月");
        case 10: return QStringLiteral("十月");
        case 11: return QStringLiteral("十一月");
        case 12: return QStringLiteral("十二月");
        default: return QStringLiteral("%1月").arg(month);
    }
}

QString ChineseCalendar::monthTitle(const QDate &today, int year, int month)
{
    if (today.year() == year && today.month() == month) {
        int ly = 0, lm = 0, ld = 0;
        const bool ok = getLunar(today, &ly, &lm, &ld);
        const QString lunarText =
            ok ? QStringLiteral("  农历") + lunarMonthName(today) + lunarDayName(today)
               : QString();
        return QStringLiteral("%1年%2月%3日  %4%5")
            .arg(today.year())
            .arg(today.month())
            .arg(today.day())
            .arg(weekdayName(today), lunarText);
    }
    return QStringLiteral("%1年%2月").arg(year).arg(month);
}

} // namespace poptools
