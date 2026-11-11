#pragma once

#include <QColor>
#include <QDate>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QSplitter;
class QTimer;
class QToolButton;

namespace poptools {

struct CalendarItem;
class CalendarStore;
class MonthView;

namespace desktopband {
class DesktopBandHost; // T-048：壁纸带宿主（ui/desktopband/DesktopBandHost）
}

// ============================================================
// 桌面日历主窗口：贴在桌面上的日程小组件。
//
// 两种模式（T-043）：
//   * 固定模式（默认）——呈现载体二选一（T-048）：
//       - 壁纸带驻入（默认，calendar/bandMode=true）：真窗口隐藏，
//         内容 grab 成 QImage 经 DesktopBandHost 直绘到 WorkerW 壁纸带
//         （高于壁纸、低于图标）；真实点击落在图标层，交互由共享
//         WH_MOUSE_LL 钩子命中转发（钩子事件 sendEvent 回本窗口，
//         交互逻辑单一来源）。带内不可能浮到应用之上，无 Z 序守护；
//       - 降级回退（bandMode=false 或带定位/DComp 失败）：顶层窗口
//         Z 序贴底（DesktopStack，1s 守护归位），右键空白透传桌面菜单。
//     不可移动/缩放；
//   * 编辑模式——置顶浮动，可拖动标题栏移动、拖边缘缩放（虚线
//     高亮提示），「完成」回到固定模式（重新驻带/贴底）。
//
// 双表示纪律：固定模式 = 真窗口隐藏 + 带呈现；内容变化（storeChanged/
// 分钟 tick/设置变化/转发点击后）重新 grab 上传。需要键盘焦点（双击
// 格子内联输入、底部输入行）时临时切回真窗口（打字挂起），输入结束
// 自动回带。
//
// 布局：自绘标题栏（模式切换 / 收起 / 关闭）
//       + 水平分栏（左：月历；右：日程列表），分栏可拖动。
//       双击月历格子 → 格子原位弹出内联输入框添加日程。
// 显示/隐藏由设置页「显示桌面日历」开关驱动（无托盘菜单项）。
// ============================================================
class CalendarWindow : public QWidget
{
    Q_OBJECT
public:
    explicit CalendarWindow(CalendarStore *store, QWidget *parent = nullptr);

    // 设置页保存后调用：外观（透明度/颜色）重应用；显示/隐藏由模块驱动
    void applySettings();

    void storeChanged(); // CalendarStore::changed 的窗口侧刷新入口

    bool isEditMode() const { return m_editMode; }

signals:
    void miniRequested(); // 用户点击「收起」（模块切到右上角迷你条）

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override; // T-048：失焦即回带
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message,
                     qintptr *result) override;

private:
    // ---- UI 构建 ----
    void buildUi();
    void applyTheme();   // 由 bg/accent 推导的全套配色应用
    void refreshTitle(); // 月份标签

    // ---- 数据刷新 ----
    void refreshLists();
    void refreshDots();
    void loadItemRow(QListWidget *list, const CalendarItem &item);

    // ---- 交互 ----
    void addFromInput(const QString &text);
    void editItem(QListWidgetItem *row);
    void removeItem(QListWidgetItem *row);
    void toggleDone(QListWidgetItem *row);
    void toggleImportant(QListWidgetItem *row);
    void moveItemRelative(QListWidgetItem *row, int delta);
    void showContextMenu(const QPoint &globalPos);
    void syncReorder(); // 拖拽落点后回写顺序
    void showInlineAdd(const QDate &date); // 双击格子：原位内联添加
    void hideInlineAdd();

    // ---- 模式切换（编辑=置顶浮动 / 固定=驻带或贴底） ----
    void setEditMode(bool on);
    void applyWindowFlags();
    void glueIfFixed(); // 固定模式呈现：驻带优先，失败降级贴底（含 1s 守护）
    void updateModeUi();

    // ---- T-048 壁纸带驻入（固定模式双表示） ----
    bool bandActive() const;
    bool ensureBandEntered();   // 懒创建/激活带宿主（失败返回 false 降级）
    void presentToBand();       // 当前内容 grab → 带呈现（几何同步）
    void degradeFromBand();     // 带丢失/带模式关闭：真窗口回归 + 贴底
    void suspendBandForTyping();// 打字挂起：带窗收起、真窗口临时可见
    void resumeBandIfSuspended();
    void maybeResumeBand();     // 焦点已不在输入控件时恢复带呈现

    CalendarStore *m_store;

    desktopband::DesktopBandHost *m_band = nullptr;
    bool m_bandSuspended = false;   // 打字挂起中（真窗口临时可见）
    bool m_bandSwitchGuard = false; // 双表示显隐切换中（hideEvent 不收带窗）

    // 标题栏
    QToolButton *m_modeBtn = nullptr;
    QToolButton *m_miniBtn = nullptr;
    QToolButton *m_closeBtn = nullptr;
    QLabel *m_modeHint = nullptr;

    // 月历列
    MonthView *m_monthView = nullptr;
    QLabel *m_monthLabel = nullptr;

    // 日程列表列（日程=待办单一列表）
    QListWidget *m_itemList = nullptr;
    QLineEdit *m_input = nullptr;
    QLabel *m_itemLabel = nullptr;
    QSplitter *m_hSplit = nullptr; // 左月历 | 右列表

    // 双击原位添加
    QLineEdit *m_inlineAdd = nullptr;
    QDate m_inlineDate;

    // 主题
    QColor m_bg = QColor("#FFFFFF");
    QColor m_accent = QColor(0x2B, 0x6F, 0xF2);
    int m_textOpacity = 100; // 文字透明度系数（背景透明度分离）
    bool m_darkText = false;

    // 模式状态
    bool m_editMode = false;    // true = 编辑模式（置顶浮动，可拖缩）
};

} // namespace poptools
