#pragma once

#include <QColor>
#include <QDate>
#include <QObject>
#include <QString>
#include <QList>

class QJsonArray;

namespace poptools {

// ============================================================
// 桌面日历数据仓库：日程条目 + 分类，JSON 本地持久化。
//
// 「数据只保存在本地，不联网」：存 AppData/PopTools/calendar.json
// （安装到 Program Files 时 exe 旁不可写，故不走 PopTools.ini 的
//  exe 旁约定，改用系统标准应用数据目录）。
//
// 日程与待办为同一列表（T-043 合并）：条目可勾选完成（打勾+删除线），
// 语义由用户自行把握。条目在 m_items 中的数组顺序即同日的显示顺序；
// 排序/移动只重排子序列，不引入显式 order 字段。
// ============================================================
struct CalendarItem
{
    QString id;            // UUID 字符串
    QDate date;            // 归属日期
    QString text;
    QString categoryId;    // 引用 CalendarCategory::id
    bool important = false;
    bool done = false;     // 完成打勾 + 删除线
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
    // 某日全部条目，按显示顺序（日程列表 / 月历圆点共用）
    QList<CalendarItem> itemsForDate(const QDate &date) const;

    QString addItem(const QDate &date, const QString &categoryId, const QString &text,
                    bool important);
    // 更新条目内容/属性（不动日期与顺序）；id 不存在返回 false
    bool updateItem(const QString &id, const QString &text, const QString &categoryId,
                    bool important);
    bool removeItem(const QString &id);
    bool setDone(const QString &id, bool done);
    // 一键安排到其他日期
    bool moveItemToDate(const QString &id, const QDate &date);
    // 同日内相邻上移/下移（delta=-1/+1）
    bool moveItem(const QString &id, int delta);
    // 拖拽重排后由 UI 回传该日完整顺序，重排 m_items 中的子序列
    void reorderItems(const QDate &date, const QStringList &orderedIds);

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
