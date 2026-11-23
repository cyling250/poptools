#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QRect>
#include <QString>

namespace poptools {

// ============================================================
// 桌面日历设置（照搬 rlqd CalendarClone/AppSettings.cs；JSON 持久化，
// 字段逐一同构）。x/y/width/height 为虚拟桌面物理像素（日历所在屏的原生
// 坐标系，矩形整体归属其中心所落的屏幕）。
// 数据落 %LOCALAPPDATA%\PopTools\calendar\（PopTools 数据目录约定）。
// ============================================================
class CalendarSettings
{
public:
    int x = 640;
    int y = 20;
    int width = 1260;
    int height = 795;
    qreal savedDpr = 0.0; // 保存本 rect 时的主屏 dpr（0=旧档未知）；加载时
                          // 与当前 dpr 比率换算保持视觉布局（T-077b）
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

    // 文件缺失 / JSON 损坏 → 返回默认设置（不报错）
    static CalendarSettings load();
    // 整文件覆写（Truncate），失败静默
    void save() const;

    // 全设置 → JSON（save 的载荷；与 fromJson 对称）
    QJsonObject toJson() const;
    // 缺键回退默认值（部分损坏仍可读）
    void fromJson(const QJsonObject &o);
};

} // namespace poptools
