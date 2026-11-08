#include "CategoryManagerDialog.h"

#include "core/calendar/CalendarStore.h"
#include "ui/ThemeColors.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace poptools {

CategoryManagerDialog::CategoryManagerDialog(CalendarStore *store, QWidget *parent)
    : QDialog(parent)
    , m_store(store)
{
    setObjectName("categoryManagerDialog"); // P-022：自定义类 QSS 一律 ID 选择器
    setWindowTitle(QStringLiteral("管理分类"));
    setMinimumWidth(360);

    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this] { reload(); });
    connect(m_store, &CalendarStore::changed, this, &CategoryManagerDialog::reload);

    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(8);
    lay->setContentsMargins(14, 14, 14, 12);

    auto *hint = new QLabel(
        QStringLiteral("分类用于给日程和待办标色；删除分类后其条目自动归入「其他」。"),
        this);
    lay->addWidget(hint);

    m_list = new QListWidget(this);
    m_list->setObjectName("categoryList");
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    lay->addWidget(m_list, 1);

    auto *btnRow = new QHBoxLayout;
    auto *addBtn = new QPushButton(QStringLiteral("添加…"), this);
    auto *renameBtn = new QPushButton(QStringLiteral("重命名…"), this);
    auto *colorBtn = new QPushButton(QStringLiteral("颜色…"), this);
    auto *delBtn = new QPushButton(QStringLiteral("删除"), this);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(renameBtn);
    btnRow->addWidget(colorBtn);
    btnRow->addWidget(delBtn);
    btnRow->addStretch();
    lay->addLayout(btnRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    if (auto *closeBtn = buttons->button(QDialogButtonBox::Close))
        closeBtn->setObjectName("okBtn");
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(addBtn, &QPushButton::clicked, this, &CategoryManagerDialog::addCategory);
    connect(renameBtn, &QPushButton::clicked, this, &CategoryManagerDialog::editCategory);
    connect(colorBtn, &QPushButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0 || row >= m_store->categories().size())
            return;
        const CalendarCategory cat = m_store->categories().at(row);
        const QColor color =
            QColorDialog::getColor(cat.color, this, QStringLiteral("分类颜色"));
        if (color.isValid())
            m_store->updateCategory(cat.id, cat.name, color);
    });
    connect(delBtn, &QPushButton::clicked, this, &CategoryManagerDialog::removeCategory);

    reload();
}

void CategoryManagerDialog::reload()
{
    const QString selectedId =
        (m_list->currentRow() >= 0 && m_list->currentItem())
            ? m_list->currentItem()->data(Qt::UserRole).toString()
            : QString();

    m_list->clear();
    for (const CalendarCategory &cat : m_store->categories()) {
        auto *item = new QListWidgetItem(cat.name, m_list);
        item->setData(Qt::UserRole, cat.id);
        QPixmap swatch(14, 14);
        swatch.fill(cat.color);
        item->setIcon(QIcon(swatch));
        if (cat.id == CalendarStore::defaultCategoryId())
            item->setToolTip(QStringLiteral("默认分类（不可删除）"));
    }
    // 恢复选中
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == selectedId) {
            m_list->setCurrentRow(i);
            return;
        }
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
}

void CategoryManagerDialog::addCategory()
{
    const QString name = QInputDialog::getText(this, QStringLiteral("添加分类"),
                                               QStringLiteral("分类名称："));
    if (name.trimmed().isEmpty())
        return;
    const QColor color = QColorDialog::getColor(QColor(0x2B, 0x6F, 0xF2), this,
                                                QStringLiteral("分类颜色"));
    m_store->addCategory(name, color);
}

void CategoryManagerDialog::editCategory()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_store->categories().size())
        return;
    const CalendarCategory cat = m_store->categories().at(row);
    const QString name = QInputDialog::getText(this, QStringLiteral("重命名分类"),
                                               QStringLiteral("分类名称："),
                                               QLineEdit::Normal, cat.name);
    if (name.trimmed().isEmpty())
        return;
    m_store->updateCategory(cat.id, name, cat.color);
}

void CategoryManagerDialog::removeCategory()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_store->categories().size())
        return;
    const CalendarCategory cat = m_store->categories().at(row);
    if (cat.id == CalendarStore::defaultCategoryId())
        return; // 默认分类不可删（按钮置灰更佳，此处兜底）
    m_store->removeCategory(cat.id);
}

} // namespace poptools
