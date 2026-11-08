#pragma once

#include <QDate>
#include <QString>

namespace poptools {

// ============================================================
// 公历 → 农历换算（1900-2100 年，经典压缩表算法）。
//
// 桌面日历的月历视图使用：格子里公历日期下方的小字标注。
// 数据表为每农历年一个整数的位压缩编码：
//   * 低 4 位（& 0xF）            闰月月份（0 = 无闰月，1-12）
//   * bit16（& 0x10000）          闰月天数（1 = 30 天，0 = 29 天）
//   * bit(16-m)（m=1..12）        第 m 个农历月是否大月（30 天）
// ============================================================
struct LunarDate
{
    int year = 0;        // 农历年（干支纪年数字，如 2026）
    int month = 0;       // 农历月 1-12
    int day = 0;         // 农历日 1-30
    bool leap = false;   // 是否闰月

    bool isValid() const { return year > 0 && month >= 1 && month <= 12 && day >= 1; }

    // 月份显示名："正月"…"腊月"（闰月前缀"闰"，如"闰六月"）
    QString monthName() const;
    // 日显示名："初一"…"三十"
    QString dayName() const;
};

namespace lunar {

// 公历日期转农历；超出 1900-02-08 ~ 2101-01-28 范围返回无效 LunarDate
LunarDate fromSolar(const QDate &date);

// 月历格小字标注：节日名 > 节气外节假日 > 每月朔日（初一）显示月名 > 平日显示日名。
// second 返回是否为节日（月历以节日色渲染）
QString dayLabel(const QDate &date, bool *isFestival = nullptr);

} // namespace lunar

} // namespace poptools
