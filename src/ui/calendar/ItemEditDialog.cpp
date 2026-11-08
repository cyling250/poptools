#include "ItemEditDialog.h"

#include "core/calendar/CalendarStore.h"
#include "ui/ThemeColors.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace poptools {

ItemEditDialog::ItemEditDialog(CalendarStore *store, const QString &existingId,
                               QWidget *parent)
    : QDialog(parent)
    , m_store(store)
    , m_itemId(existingId)
{
    setObjectName("itemEditDialog"); // P-022：自定义类 QSS 一律 ID 选择器
    setModal(true);
    setMinimumWidth(340);
    setWindowTitle(QStringLiteral("日程"));

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(16, 16, 16, 12);

    auto *form = new QFormLayout;
    form->setSpacing(8);
    form->setLabelAlignment(Qt::AlignRight);

    m_textEdit = new QLineEdit(this);
    m_textEdit->setPlaceholderText(QStringLiteral("输入内容"));
    form->addRow(QStringLiteral("内容:"), m_textEdit);

    m_categoryCombo = new QComboBox(this);
    for (const CalendarCategory &cat : store->categories())
        m_categoryCombo->addItem(cat.name, cat.id);
    form->addRow(QStringLiteral("分类:"), m_categoryCombo);

    m_importantCheck = new QCheckBox(QStringLiteral("重要事项（加粗高亮）"), this);
    form->addRow(QString(), m_importantCheck);
    mainLayout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (auto *okBtn = buttons->button(QDialogButtonBox::Ok)) {
        okBtn->setObjectName("okBtn");
        okBtn->setText(QStringLiteral("保存"));
    }
    if (auto *cancelBtn = buttons->button(QDialogButtonBox::Cancel))
        cancelBtn->setObjectName("cancelBtn");

    QPushButton *deleteBtn = nullptr;
    if (!m_itemId.isEmpty()) {
        deleteBtn = buttons->addButton(QStringLiteral("删除"), QDialogButtonBox::DestructiveRole);
        deleteBtn->setObjectName("cancelBtn");
    }
    mainLayout->addWidget(buttons);

    // 编辑态：先载入现值
    if (!m_itemId.isEmpty()) {
        for (const CalendarItem &it : store->items()) {
            if (it.id != m_itemId)
                continue;
            m_textEdit->setText(it.text);
            const int idx = m_categoryCombo->findData(it.categoryId);
            m_categoryCombo->setCurrentIndex(qMax(0, idx));
            m_importantCheck->setChecked(it.important);
            break;
        }
        connect(deleteBtn, &QPushButton::clicked, this, [this] {
            m_store->removeItem(m_itemId);
            reject(); // 已删除，按取消关闭（调用方以 dialog code 区分）
        });
    }

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_textEdit, &QLineEdit::returnPressed, this, &QDialog::accept);

    // 主题化跟随系统明暗（构造末尾调用，涉及子控件样式）
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this] { applyTheme(); });
    applyTheme();

    m_textEdit->setFocus();
}

QString ItemEditDialog::text() const
{
    return m_textEdit->text().trimmed();
}

QString ItemEditDialog::categoryId() const
{
    return m_categoryCombo->currentData().toString();
}

bool ItemEditDialog::important() const
{
    return m_importantCheck->isChecked();
}

void ItemEditDialog::applyTheme()
{
    const ThemeColors c = uiThemeColors(systemSchemeDark());
    setStyleSheet(QStringLiteral(
        "#itemEditDialog { background: %1; }"
        "QLineEdit { background: %2; border: 1px solid %3; border-radius: 6px;"
        "  padding: 5px 8px; color: %4; }"
        "QLineEdit:focus { border-color: %5; }"
        "QComboBox { background: %2; border: 1px solid %3; border-radius: 6px;"
        "  padding: 4px 8px; color: %4; }"
        "QCheckBox { color: %4; }"
        "QLabel { color: %4; }"
        "QPushButton { background: %6; border: 1px solid %7; border-radius: 6px;"
        "  padding: 4px 12px; color: %4; }"
        "QPushButton:hover { background: %8; border-color: %5; color: %5; }"
        "#okBtn { background: %5; color: #FFFFFF; border: 1px solid %5;"
        "  padding: 5px 16px; font-weight: 600; }"
        "#okBtn:hover { background: %9; color: #FFFFFF; }"
        "#cancelBtn { padding: 5px 16px; }")
        .arg(c.cardBg, c.editBg, c.editBorder, c.body, c.accent,
             c.ctrlBg, c.ctrlBorder, c.ctrlHover, c.accentHover));
}

} // namespace poptools
