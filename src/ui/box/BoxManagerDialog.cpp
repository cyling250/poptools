#include "BoxManagerDialog.h"

#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace poptools {

BoxManagerDialog::BoxManagerDialog(BoxCatalog &catalog, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::WindowCloseButtonHint)
    , m_catalog(catalog)
{
    setWindowTitle(QStringLiteral("收纳盒管理"));
    // 关闭即销毁（P-058 纪律）：hide-only 会让 openManager 的"已开着"分支
    // 抬不起隐藏窗；销毁后 destroyed 回调复位 m_manager，下次托盘「收纳盒管理」
    // 打开正常新建
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);
    setFixedSize(440, 440);

    auto *layout = new QVBoxLayout(this);

    auto *hint = new QLabel(QStringLiteral(
        "每个收纳盒绑定一个真实文件夹：盒内条目随文件夹实时同步；\n"
        "整理文件在资源管理器进行，删除盒只是解除绑定，不会删文件。"), this);
    hint->setStyleSheet(QStringLiteral("color: #9a9a9a; font-size: 12px;"));
    layout->addWidget(hint);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget { background: #23252d; color: #e6e6e6; border: 1px solid #3a3d47;"
        " border-radius: 4px; }"
        "QListWidget::item { padding: 4px 6px; }"
        "QListWidget::item:selected { background: #3d5a8a; }"));
    layout->addWidget(m_list, 1);

    auto addBtn = [&](QHBoxLayout *row, const QString &text, auto slot) {
        auto *b = new QPushButton(text, this);
        connect(b, &QPushButton::clicked, this, slot);
        row->addWidget(b);
        return b;
    };

    auto *row1 = new QHBoxLayout();
    addBtn(row1, QStringLiteral("新建空盒"), [this] { createManagedBox(); });
    addBtn(row1, QStringLiteral("绑定文件夹…"), [this] { bindFolder(); });
    layout->addLayout(row1);

    auto *row2 = new QHBoxLayout();
    m_renameBtn = addBtn(row2, QStringLiteral("改名"), [this] { renameSelected(); });
    m_removeBtn = addBtn(row2, QStringLiteral("解除绑定"), [this] { removeSelected(); });
    m_openBtn = addBtn(row2, QStringLiteral("打开文件夹"), [this] { openSelectedDir(); });
    layout->addLayout(row2);

    auto *row3 = new QHBoxLayout();
    m_adjustBtn = addBtn(row3, QStringLiteral("调整位置和列数"), [this] { adjustSelected(); });
    row3->addStretch(1);
    addBtn(row3, QStringLiteral("关闭"), [this] { close(); });
    layout->addLayout(row3);

    reloadList();
}

void BoxManagerDialog::focusBox(const QString &boxId)
{
    reloadList();
    for (int i = 0; i < m_catalog.boxes().size(); ++i) {
        if (m_catalog.boxes().at(i).id == boxId) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

// 目录 → 列表重建（每盒显示条目数）；无盒时禁用改名/解绑/打开/调整按钮。
// T-062 修复: 条目数原用 QDir::count()——它等价 entryList() 且计入 "."/".."，
// 空盒显示"（2 项）"且与面板 refreshEntries 恒差 2（BUG-04）；改用与面板
// 同一过滤口径
void BoxManagerDialog::reloadList()
{
    m_list->clear();
    for (const BoxDef &d : m_catalog.boxes()) {
        const int count =
            QDir(d.dir).entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot).size();
        auto *item = new QListWidgetItem(
            QStringLiteral("%1（%2 项）").arg(d.name).arg(count), m_list);
        item->setToolTip(d.dir);
    }
    const bool has = !m_catalog.boxes().isEmpty();
    for (QPushButton *b : { m_renameBtn, m_removeBtn, m_openBtn, m_adjustBtn })
        b->setEnabled(has);
    if (m_list->count() > 0 && m_list->currentRow() < 0)
        m_list->setCurrentRow(0);
}

void BoxManagerDialog::createManagedBox()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("新建空盒"),
                                               QStringLiteral("盒名："), QLineEdit::Normal,
                                               QStringLiteral("收纳盒"), &ok);
    if (!ok)
        return;
    BoxDef d;
    d.name = name.isEmpty() ? QStringLiteral("收纳盒") : name;
    const QString dir = BoxCatalog::managedDirFor(d.name);
    if (!QDir().mkpath(dir)) {
        QMessageBox::warning(this, QStringLiteral("新建失败"),
                             QStringLiteral("无法创建托管文件夹：\n") + dir);
        return;
    }
    d.dir = QDir::toNativeSeparators(dir);
    m_catalog.addBox(d);
    reloadList();
    emit catalogChanged();
}

void BoxManagerDialog::bindFolder()
{
    // 进程级记忆上次绑定目录，作 QFileDialog 起始位置（有意为之的 static，非笔误）
    static QString lastDir;
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("绑定已有文件夹"), lastDir);
    if (dir.isEmpty())
        return;
    lastDir = dir;
    BoxDef d;
    d.name = QFileInfo(dir).fileName();
    d.dir = QDir::toNativeSeparators(dir);
    m_catalog.addBox(d);
    reloadList();
    emit catalogChanged();
}

void BoxManagerDialog::renameSelected()
{
    const int row = m_list->currentRow();
    if (row < 0)
        return;
    const BoxDef d = m_catalog.boxes().at(row);
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("改名"),
                                               QStringLiteral("盒名："), QLineEdit::Normal,
                                               d.name, &ok);
    if (!ok || name.isEmpty() || name == d.name)
        return;
    m_catalog.renameBox(d.id, name);
    reloadList();
    m_list->setCurrentRow(row);
    emit catalogChanged();
}

void BoxManagerDialog::removeSelected()
{
    const int row = m_list->currentRow();
    if (row < 0)
        return;
    const BoxDef d = m_catalog.boxes().at(row);
    const auto ret = QMessageBox::question(
        this, QStringLiteral("解除绑定"),
        QStringLiteral("从桌面移除收纳盒「%1」？\n绑定的文件夹与其中的文件不会被删除。")
            .arg(d.name));
    if (ret != QMessageBox::Yes)
        return;
    m_catalog.removeBox(d.id);
    reloadList();
    emit catalogChanged();
}

void BoxManagerDialog::openSelectedDir()
{
    const int row = m_list->currentRow();
    if (row < 0)
        return;
    const QString dir = m_catalog.boxes().at(row).dir;
    if (QDir(dir).exists())
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void BoxManagerDialog::adjustSelected()
{
    const int row = m_list->currentRow();
    if (row < 0)
        return;
    emit adjustRequested(m_catalog.boxes().at(row).id);
}

} // namespace poptools
