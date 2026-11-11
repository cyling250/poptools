#pragma once

#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QPixmap>
#include <QSet>
#include <QWidget>

class QFileSystemWatcher;
class QLineEdit;

namespace poptools {

class BoxStore;

namespace desktopband {
class DesktopBandHost; // T-048：壁纸带宿主（ui/desktopband/DesktopBandHost）
}

// ============================================================
// 桌面收纳盒窗口：贴在桌面层的分类卡片（一个盒子一个窗口）。
//
// 交互 = 手机桌面文件夹：
//   * 从桌面把文件拖进卡片 → 文件真实移入盒子目录（桌面变清爽）；
//   * 点条目打开；右键条目 → 打开 / 打开所在位置 / 移出到桌面；
//   * 标题栏右侧：⚙ 打开「几行几列」设置菜单（列数/行数 1-8），
//     − 折叠成横条（宽度保持展开宽度，− 变 +）；
//   * 双击标题【文字】内联改名；右键标题 → 重命名 / 打开目录 /
//     解散收纳盒（文件全部移回桌面并删盒） / 删除收纳盒
//     （只删盒子不删文件）；标题栏拖动移动位置（位置持久化）；
//   * 空白处右键 → 透出系统桌面菜单（与桌面日历一致）。
//
// 尺寸由「几行几列」决定：宽 = 2×边距 + cols×格宽，展开高 =
// 标题栏 + rows×格高（条目超出 rows×cols 截断，右键标题打开目录）。
//
// 呈现载体（T-048）：box/bandMode=true（默认）时驻入壁纸带直绘
//（DesktopBandHost，真窗口隐藏 + 内容 grab 上传 + 钩子转发交互）；
// 失败降级/逃生口关闭时回退旧贴底路径——顶层窗口 Z 序经 DesktopStack
// 钉在日历层之上（Progman 正上方，P-031：SetParent 进桌面层内容不被
// 合成，故贴 Z 序）；WM_MOUSEACTIVATE → MA_NOACTIVATE 不抢焦点
// 不抬窗；目录内容经 QFileSystemWatcher 自动刷新。
//
// ⚠ 已知限制（T-048，如实呈现）：带内窗口收不到 OLE 拖放——「从桌面
// 拖文件进盒子」在带模式下不可用（拖放会被图标层当桌面拖放处理）。
// 备选逃生口：ini 设 box/bandMode=false 回退旧贴底路径即恢复拖入。
// 点击/右键/改名/折叠/移出等其余交互不受影响。
// ============================================================
class BoxWindow : public QWidget
{
    Q_OBJECT
public:
    explicit BoxWindow(BoxStore *store, const QString &boxId, QWidget *parent = nullptr);
    QString boxId() const { return m_boxId; }

    // T-048-FIX（P3-2）：设置变更后重估呈现载体（box/bandMode 逃生口
    // 即时生效；模块 settingsChanged 链路调用）
    void applySettings();

signals:
    void storageError(const QString &message); // 移动失败（模块转托盘气泡）

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override; // T-048：失焦即回带
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    // ---- 几何 ----
    int cardWidth() const;            // 2×边距 + m_cols×格宽
    QSize windowSize() const;         // 折叠态 = 标题横条；展开态 = 标题 + rows 行
    QRect titleRect() const;          // 标题栏（拖动区/盒子菜单）
    QRect nameTextRect() const;       // 标题【文字】区（双击改名判定）
    QRect gearRect() const;           // ⚙ 几行几列按钮
    QRect collapseRect() const;       // −/+ 折叠按钮
    QList<QRect> itemRects() const;
    int hitItem(const QPoint &localPos) const;
    void applyGeometry();             // 按当前 cols/rows/折叠/条目数落定尺寸
    void syncMeta();                  // 仓库变化：同步名称/行列/折叠

    // ---- 行为 ----
    void refresh();                   // 重读目录 + 重绘（QFileSystemWatcher/拖放后）
    void prefetchIcons();             // 条目图标后台预取（完成后 update 刷新）
    void openSizeMenu();              // ⚙：几行几列设置菜单
    void startRename();               // 双击标题文字：内联编辑框
    void finishRename(bool commit);
    void dissolve();                  // 解散：文件移回桌面 + 删盒
    void glueIfFixed();               // 呈现载体：驻带优先，失败降级贴底

    // ---- T-048 壁纸带驻入（与日历同款双表示 + 打字挂起） ----
    bool bandActive() const;
    bool ensureBandEntered();
    void presentToBand();
    void degradeFromBand();
    void suspendBandForTyping();
    void resumeBandIfSuspended();
    void maybeResumeBand();

    BoxStore *m_store = nullptr;
    QString m_boxId;
    QString m_name;            // 标题快照
    QString m_dir;             // 目录快照
    bool m_dirExists = false;  // 目录存在性快照（refresh 缓存，paint 只读成员，R6-C3）
    int m_cols = 4;            // 网格列数
    int m_rows = 4;            // 网格行数（可展示行数）
    bool m_collapsed = false;
    QList<QFileInfo> m_items;  // 目录内容（截断展示 rows×cols）
    int m_totalCount = 0;

    // 图标缓存（shell 提取在 QThreadPool 后台做，主线程只查缓存绘制；
    // 文件夹图标自绘不走缓存）
    QHash<QString, QPixmap> m_iconCache;  // 原生路径 → 已提取图标
    QSet<QString> m_iconPending;          // 后台提取在途（防重复派发）

    // 交互状态
    int m_hoverItem = -1;
    bool m_dropHover = false;
    bool m_draggingWindow = false; // 标题栏拖动中
    bool m_moveInFlight = false;   // 拖放收纳移动中（worker 线程）——显示"收纳中"
    QPoint m_dragOffset;
    bool m_hoverGear = false;
    bool m_hoverCollapse = false;
    QLineEdit *m_renameEdit = nullptr; // 双击改名的内联编辑框（懒创建）
    QFileSystemWatcher *m_watcher = nullptr;

    desktopband::DesktopBandHost *m_band = nullptr; // T-048：壁纸带宿主（懒创建）
    bool m_bandSuspended = false;      // 打字挂起中（真窗口临时可见）
    bool m_bandSwitchGuard = false;    // 双表示显隐切换中（hideEvent 不收带窗）
};

} // namespace poptools
