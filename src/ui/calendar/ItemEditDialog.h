#pragma once

#include <QDate>
#include <QDialog>

class QComboBox;
class QCheckBox;
class QLineEdit;
class QPushButton;

namespace poptools {

class CalendarStore;

// ============================================================
// 日程条目新增与编辑对话框：文本 + 分类 + 重要标记（日程=待办
// 单一列表，T-043 起不再区分类型）。
// 编辑态额外提供「删除」按钮（复用同一对话框，避免两套 UI）。
// ============================================================
class ItemEditDialog : public QDialog
{
    Q_OBJECT
public:
    // 新增：existingId 传空串
    ItemEditDialog(CalendarStore *store, const QString &existingId = QString(),
                   QWidget *parent = nullptr);

    QString text() const;
    QString categoryId() const;
    bool important() const;

private:
    void applyTheme();

    CalendarStore *m_store;
    QString m_itemId;
    QLineEdit *m_textEdit;
    QComboBox *m_categoryCombo;
    QCheckBox *m_importantCheck;
};

} // namespace poptools
