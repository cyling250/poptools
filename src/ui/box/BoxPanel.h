#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QSet>
#include <QVector>
#include <optional>

#include "core/box/BoxLayout.h"
#include "core/box/BoxCatalog.h"
#include "core/box/IconService.h"
#include "ui/box/BoxRenderer.h"
#include "ui/shared/DesktopSurfaceHost.h"

#include <QTimer>

class QFileSystemWatcher;

namespace poptools {

// ============================================================
// 桌面收纳盒面板（T-060 文件夹门户式：一个盒 = 一个真实文件夹的桌面视图，
// 一盒一面板实例，多盒各自注册到桌面表面宿主）。
// 条目 = 绑定目录实时列表（QFileSystemWatcher 自动同步），双击打开文件；
// 尺寸 = 格子整数倍（列数手动、行数随条目自动增高，超容量翻页）。
// 条目整理回归资源管理器：盒是视图，不搬文件。
// ============================================================
class BoxPanel : public QObject, public DesktopPanel
{
    Q_OBJECT
public:
    explicit BoxPanel(BoxCatalog *catalog, const QString &boxId, QObject *parent = nullptr);
    ~BoxPanel() override;

    QString boxId() const { return m_boxId; }

    // ---- DesktopPanel ----
    QRect rectPhysical() const override;
    QImage *frame() override;
    void render() override;
    void onMove(const QPoint &p) override;
    void onLeave() override;
    void onClick(const QPoint &p) override;
    void onDoubleClick(const QPoint &p) override;
    void onDutyStarted() override;
    void onDutyStopped() override;

    // 模块/管理器入口
    void reloadDef();        // 定义被外部（管理器/托盘）修改后重读并重绘
    void enterAdjustMode();  // 盒头 ✛ 调整位置和列数（尺寸恒为格子整数倍）

private:
    const BoxDef def() const;
    void refreshEntries(); // 重列绑定目录 → 自动定尺寸 → 重绘
    void ensureVisibleIcons(int capacity, int page);
    void renderFrame(const QRect &commitDirty); // 实绘 + 按提交脏矩形合成发布（空=整帧）
    void renderDuringAdjust();
    void launchEntry(int index);
    void updateHover(const QPoint &p);
    void handleSingleClick(const QPoint &p);
    QRect hoverVisualRect(int cell, int header) const;   // 悬停会改到的屏幕区域
    QRect selectedVisualRect(int globalIndex) const;     // 选中高亮所在格子矩形
    QPoint logicalPos(const QPoint &physical) const;
    bool eventsAccepted() const { return !m_modal && !m_adjusting; }

    QString m_boxId;
    BoxCatalog *m_catalog = nullptr;
    QImage m_staging; // 面板私有整帧
    std::unique_ptr<BoxRenderer> m_renderer;
    IconService *m_icons = nullptr;
    QHash<QString, QImage> m_iconCache;
    QVector<BoxItem> m_items; // 绑定目录实时条目（名字序）
    std::optional<BoxLayout> m_layout;
    QPoint m_hover;
    int m_lastHoverCell = -1;   // 上次重绘时的悬停格子（-1=无；目标变化才重绘）
    int m_lastHoverHeader = -1; // 上次重绘时的悬停头部按钮
    int m_selected = -1;
    bool m_modal = false;
    bool m_adjusting = false;
    QPointer<QWidget> m_modalWidget;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer m_refreshDebounce; // 目录变化防抖（批量文件操作合并为一次重列）
    QTimer *m_wallpaperTimer = nullptr;
};

} // namespace poptools
