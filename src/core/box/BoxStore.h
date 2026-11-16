#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <QVector>

namespace poptools {

// 收纳盒条目：软件引用（.lnk/.exe 绝对路径）+ 显示名。
// 语义是安卓桌面文件夹式的「虚拟分组」——只存引用不搬文件，不是真实文件夹。
struct BoxEntry
{
    QString name;
    QString target;
};

inline bool operator==(const BoxEntry &a, const BoxEntry &b)
{
    return a.name == b.name && a.target == b.target;
}

// ============================================================
// 收纳盒设置（物理像素坐标，主屏共享画布坐标系；JSON 持久化）
// 数据落 %LOCALAPPDATA%\PopTools\box\（PopTools 数据目录约定）
// ============================================================
class BoxSettings
{
public:
    int x = 1400;
    int y = 220;
    int width = 372;
    int height = 430;
    int cols = 4; // 列数（2~8）；行数由高度推导
    bool hidden = false;
    bool glassEffect = true;
    int fontScale = 100;  // 70-160
    int page = 0;         // 记忆浏览页（回到上次位置）

    QRect rect() const { return QRect(x, y, width, height); }

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &o);
};

// ============================================================
// 收纳盒仓库：设置 + 条目列表，单文件 box.json。
// QSaveFile 原子写；损坏文件先备份 .bad 再空载（防坏档反复崩溃）。
// 每次变更即落盘（与 CalendarSettings 同策略）。
// ============================================================
class BoxStore
{
public:
    static BoxStore load();

    void save() const;

    const QVector<BoxEntry> &entries() const { return m_entries; }
    const BoxSettings &settings() const { return m_settings; }
    BoxSettings &settings() { return m_settings; }

    void addEntry(const BoxEntry &e);              // 追加 + 落盘
    void removeEntry(int index);                   // 删除 + 落盘
    void moveEntry(int from, int to);              // 重排（to 为目标下标，其余顺移）+ 落盘
    void setEntryName(int index, const QString &name);
    void clearEntries();

    static QString dataDir();   // %LOCALAPPDATA%\PopTools\box
    static QString storePath(); // ...\box.json
    // 数据目录重定向（单测注入 QTemporaryDir 用；空串 = 恢复默认路径）
    static void setDataDirOverride(const QString &dir);

    QJsonObject toJson() const;
    bool fromJson(const QString &json);

private:
    BoxSettings m_settings;
    QVector<BoxEntry> m_entries;
};

} // namespace poptools
