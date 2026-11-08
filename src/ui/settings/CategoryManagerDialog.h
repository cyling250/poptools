#pragma once

#include <QColor>
#include <QDialog>

class QListWidget;

namespace poptools {

class CalendarStore;

// ============================================================
// 日历分类管理：增删改分类名与颜色，改动即时写入 CalendarStore
//（本对话框是数据管理入口，与设置页的「确定/取消」语义无关）。
// 删除分类时其条目自动改挂「其他」。
// ============================================================
class CategoryManagerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CategoryManagerDialog(CalendarStore *store, QWidget *parent = nullptr);

private:
    void reload();
    void addCategory();
    void editCategory();
    void removeCategory();

    CalendarStore *m_store;
    QListWidget *m_list = nullptr;
};

} // namespace poptools
