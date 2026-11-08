#pragma once

#include <QColor>
#include <QDate>
#include <QObject>
#include <QString>
#include <QList>

class QJsonArray;

namespace poptools {

// ============================================================
// 桌面日历数据仓库：日程/待办条目 + 分类，JSON 本地持久化。
//
// 「数据只保存在本地，不联网」：存 AppData/PopTools/calendar.json
// （安装到 Program Files 时 exe 旁不可写，故不走 PopTools.ini 的
//  exe 旁约定，改用系统标准应用数据目录）。
//
// 条目在 m_items 中的数组顺序即同日同组（日程/待办）的显示顺序；
// 排序/移动只重排子序列，不引入显式 order 字段。
// ============================================================
struct CalendarItem
{
    QString id;            // UUID 字符串
    QDate date;            // 归属日期
    bool todo = false;     // false = 日程，true = 待办
    QString text;
    QString categoryId;    // 引用 CalendarCategory::id
    bool important = false;
    bool done = false;     // 仅待办使用（完成打勾 + 删除线）
};

struct CalendarCategory
{
    QString id;
    QString name;
    QColor color;
};

class CalendarStore : public QObject
{
    Q_OBJECT
public:
    explicit CalendarStore(QObject *parent = nullptr);
    // 测试/工具注入用：以指定 JSON 路径构造独立实例（不影响标准目录）
    explicit CalendarStore(const QString &jsonPath, QObject *parent = nullptr);

    // 从数据文件加载（构造后调用一次；文件不存在时播种默认分类）
    void load();
    // 原子落盘（QSaveFile）；每次修改后自动调用
    void save();

    // ---- 条目 ----
    const QList<CalendarItem> &items() const { return m_items; }
    // 某日的待办（todo=true）或日程（todo=false），按显示顺序
    QList<CalendarItem> itemsForDate(const QDate &date, bool todo) const;
    // 某日全部条目（月历圆点标记用）
    QList<CalendarItem> itemsForDate(const QDate &date) const;

    QString addTodo(const QDate &date, const QString &categoryId, const QString &text,
                    bool important);
    QString addSchedule(const QDate &date, const QString &categoryId, const QString &text,
                        bool important);
    // 更新条目内容/属性（不动日期与顺序）；id 不存在返回 false
    bool updateItem(const QString &id, const QString &text, const QString &categoryId,
                    bool important);
    bool removeItem(const QString &id);
    bool setDone(const QString &id, bool done);
    // 待办一键安排到其他日期（日程同样可用）
    bool moveItemToDate(const QString &id, const QDate &date);
    // 同日同组内相邻上移/下移（delta=-1/+1）
    bool moveItem(const QString &id, int delta);
    // 拖拽重排后由 UI 回传该组完整顺序，重排 m_items 中的子序列
    void reorderItems(const QDate &date, bool todo, const QStringList &orderedIds);

    // ---- 分类 ----
    const QList<CalendarCategory> &categories() const { return m_categories; }
    QString addCategory(const QString &name, const QColor &color);
    bool updateCategory(const QString &id, const QString &name, const QColor &color);
    // 删除分类：引用它的条目自动改挂默认分类（"其他"），分类至少保留一个
    bool removeCategory(const QString &id);
    // 分类颜色（未知 id 回退默认灰蓝）
    QColor categoryColor(const QString &id) const;
    QString categoryName(const QString &id) const;

    static QString defaultCategoryId();

signals:
    // 任意数据变更（条目/分类/增删改/顺序），UI 层连接后整体刷新
    void changed();

private:
    void ensureDefaultCategories();
    QList<CalendarItem>::iterator findItem(const QString &id);
    void toJson(QJsonArray *items, QJsonArray *categories) const;
    void fromJson(const QJsonArray &items, const QJsonArray &categories);

    QList<CalendarItem> m_items;
    QList<CalendarCategory> m_categories;
    QString m_filePath;
};

} // namespace poptools
