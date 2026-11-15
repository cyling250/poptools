#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QRect>
#include <QString>

namespace poptools {

// ============================================================
// 桌面日历设置（照搬 rlqd CalendarClone/AppSettings.cs；JSON 持久化，
// 字段逐一同构）。坐标为共享画布上的物理像素（主屏）。
// 数据落 %LOCALAPPDATA%\PopTools\calendar\（PopTools 数据目录约定）。
// ============================================================
class CalendarSettings
{
public:
    int x = 640;
    int y = 20;
    int width = 1260;
    int height = 795;
    bool hidden = false;
    bool showLunar = true;
    bool showWeekNumbers = true;
    bool showEvents = true;
    bool glassEffect = true;
    int fontScale = 100; // 70-160
    bool showGuide = true;
    bool showNextMonthPill = true;

    QRect rect() const { return QRect(x, y, width, height); }

    static QString dataDir();      // %LOCALAPPDATA%\PopTools\calendar
    static QString settingsPath(); // ...\settings.json
    static QString tasksPath();    // ...\tasks.json
    // 数据目录重定向（单测注入 QTemporaryDir 用；空串 = 恢复默认路径）
    static void setDataDirOverride(const QString &dir);

    static CalendarSettings load();
    void save() const;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &o);
};

} // namespace poptools
