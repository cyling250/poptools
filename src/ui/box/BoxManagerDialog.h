#pragma once

#include <QDialog>
#include <QPointer>

#include "core/box/BoxCatalog.h"

class QListWidget;
class QPushButton;

namespace poptools {

// ============================================================
// 收纳盒管理对话框（T-060 多盒）：新建空盒（数据目录托管）/ 绑定已有
// 文件夹 / 改名 / 删除绑定（绝不删文件夹）/ 打开文件夹 / 调整位置和列数。
// 结构性变更经 catalogChanged 通知模块重建面板实例。
// P-058/P-059 纪律：finished → deleteLater 由打开方负责，定位由打开方
// centerOnPanelRect 锚定。
// ============================================================
class BoxManagerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BoxManagerDialog(BoxCatalog &catalog, QWidget *parent = nullptr);
    void focusBox(const QString &boxId); // 定位选中指定盒；现入口仅托盘「收纳盒管理」（传空串）

signals:
    void catalogChanged();               // 结构变更（模块重建面板实例）
    void adjustRequested(const QString &boxId); // 调整位置和列数（模块转面板）

private slots:
    void applyTheme(); // Fluent token 主题应用（构造末尾 + 系统明暗切换）

private:
    void reloadList();
    void createManagedBox(); // 新建空盒（托管目录）
    void bindFolder();       // 绑定已有文件夹
    void renameSelected();
    void removeSelected();   // 解除绑定（不删文件）
    void openSelectedDir();
    void adjustSelected();

    BoxCatalog &m_catalog;
    QListWidget *m_list = nullptr;
    QPushButton *m_renameBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QPushButton *m_openBtn = nullptr;
    QPushButton *m_adjustBtn = nullptr;
};

} // namespace poptools
