#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class QJsonArray;

namespace poptools {

// ============================================================
// 桌面收纳盒数据仓库：若干「盒子」（桌面上的分类框框），每个
// 盒子对应磁盘上一个真实目录（默认建在桌面，文件真实移入，
// 桌面随之清爽，同盘移动即时完成）。
//
// 盒子内条目不存数据库——直接读目录内容（QFileSystemWatcher
// 刷新），仓库只存盒子元数据（名称/目录/位置/几行几列/折叠）。
// 存 AppData/PopTools/boxes.json（与 calendar.json 同目录约定）。
// 套路照抄 CalendarStore：双构造注入路径供测试、每次修改即时
// 落盘 + changed() 广播、version 字段。
// ============================================================
struct DesktopBox
{
    QString id;          // UUID 字符串
    QString name;        // 盒子标题（默认与目录名一致）
    QString dir;         // 收纳目标目录（真实存在或可创建）
    int x = 100;         // 桌面位置（逻辑坐标，主屏）
    int y = 100;
    int cols = 4;        // 网格列数（宽 = 2×边距 + cols×格宽）
    int rows = 4;        // 网格行数（高 = 标题栏 + rows×格高，超出截断）
    bool collapsed = false; // 折叠成横条（只留标题栏）
};

class BoxStore : public QObject
{
    Q_OBJECT
public:
    explicit BoxStore(QObject *parent = nullptr);
    // 测试/工具注入用：以指定 JSON 路径构造独立实例（不影响标准目录）
    explicit BoxStore(const QString &jsonPath, QObject *parent = nullptr);

    // 从数据文件加载（构造后调用一次；文件不存在时保持空列表）
    void load();
    // 原子落盘（QSaveFile）；每次修改后自动调用
    void save();

    const QList<DesktopBox> &boxes() const { return m_boxes; }

    // 新建盒子：目录不存在时自动创建（目录已存在则直接收纳进去）
    QString addBox(const QString &name, const QString &dir, int x, int y);
    bool removeBox(const QString &id); // 只删盒子，不动磁盘目录
    bool renameBox(const QString &id, const QString &name);
    bool setBoxPos(const QString &id, int x, int y);
    // 网格列数 / 行数（几行几列，均钳制 1-8；行数 = 可展示行数，超出截断）
    bool setBoxCols(const QString &id, int cols);
    bool setBoxRows(const QString &id, int rows);
    // 折叠/展开（折叠 = 只留标题横条，宽度不变）
    bool setBoxCollapsed(const QString &id, bool collapsed);

    // ---- 文件收纳操作（供窗口层调用） ----
    // 把一批文件/目录移动进 dir（重名自动加 "(2)" 序号），
    // 返回失败条目的文件名列表（跨盘移动走复制+删除兜底）
    static QStringList moveUrlsIntoDir(const QString &dir, const QList<QUrl> &urls);
    // 把 fromDir 内全部条目（含子目录）移入 toDir（解散收纳盒的搬运用）。
    // 纯文件系统操作、不触碰仓库状态，可在工作线程调用；返回失败文件名
    // 列表（有失败时调用方保留现场）
    static QStringList moveDirContents(const QString &fromDir, const QString &toDir);
    // 目标目录内生成不重名的落点路径
    static QString uniqueTargetPath(const QString &dir, const QString &fileName);

signals:
    // 任意数据变更（盒子增删改/位置），UI 层连接后整体刷新
    void changed();

private:
    void toJson(QJsonArray *boxes) const;
    void fromJson(const QJsonArray &boxes);

    QList<DesktopBox> m_boxes;
    QString m_filePath;
};

} // namespace poptools
