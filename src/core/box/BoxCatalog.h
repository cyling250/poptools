#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace poptools {

// ============================================================
// 收纳盒定义（T-060 文件夹门户式）：一个盒 = 一个真实文件夹的桌面视图。
// 条目不落盘（就是文件夹内容实时列表），只存盒的几何与绑定关系。
// ============================================================
struct BoxDef
{
    QString id;      // UUID（无花括号）
    QString name;    // 显示名
    QString dir;     // 绑定文件夹绝对路径（原生分隔符）
    int x = 60;      // 主屏物理坐标（左上）
    int y = 560;
    int cols = 4;    // 列数（2~8）；行数随条目数自动（见 BoxLayout::compute）
    int page = 0;    // 记忆浏览页
    bool hidden = false;

    QJsonObject toJson() const;
    static BoxDef fromJson(const QJsonObject &o, bool *ok = nullptr);
};

// ============================================================
// 收纳盒仓库：多盒定义列表，单文件 boxes.json（v2 格式）。
// QSaveFile 原子写；损坏文件先备份 .bad 再空载（防坏档反复崩溃）。
// 旧 v1（虚拟条目 box.json）不迁移：T-059 交付当晚无存量。
// ============================================================
class BoxCatalog
{
public:
    static BoxCatalog load();

    void save() const;

    const QVector<BoxDef> &boxes() const { return m_boxes; }
    int indexOf(const QString &id) const;

    // 结构性操作（各自落盘）
    QString addBox(BoxDef def);            // 补 id 后追加，返回 id
    void removeBox(const QString &id);     // 仅解除绑定，绝不删文件夹
    void renameBox(const QString &id, const QString &name);
    void updateBox(const QString &id, const BoxDef &def); // 整体替换（几何/页/hidden）
    void setHiddenAll(bool hidden);

    // 新建托管盒的默认目录：%LOCALAPPDATA%\PopTools\box\boxes\<name>[-2]
    static QString dataDir();   // %LOCALAPPDATA%\PopTools\box
    static QString storePath(); // ...\boxes.json
    static QString managedDirFor(const QString &name); // 重名自动 -2 顺延
    static void setDataDirOverride(const QString &dir); // 单测注入

    QJsonObject toJson() const;
    bool fromJson(const QString &json);

private:
    QVector<BoxDef> m_boxes;
};

} // namespace poptools
