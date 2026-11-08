#pragma once

#include <QColor>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QSplitter;
class QToolButton;

namespace poptools {

struct CalendarItem;
class CalendarStore;
class MonthView;

// ============================================================
// 桌面日历主窗口（无边框、可置顶、贴边收起、透明度可调）。
//
// 布局：自绘标题栏（拖动移动/置顶/收起/关闭）
//       + 水平分栏（左：月历；右：垂直分栏的日程与待办列表）
//       ，两处分栏都可拖动调整（对齐小红书原版「窗口及各栏尺寸
//       都能拖动调整」）。
//
// 边缘收起：拖动到屏幕左/右/上边缘 18px 内自动收成贴边小条
//（EdgeTab，悬停或点击展开），展开后 1.5s 内不再重复吸附。
// 收起/展开的窗口几何记忆在本类内部，不落设置。
// ============================================================
class CalendarWindow : public QWidget
{
    Q_OBJECT
public:
    explicit CalendarWindow(CalendarStore *store, QWidget *parent = nullptr);

    // 设置页保存后调用：置顶/贴边/透明度/颜色全量重应用
    void applySettings();

    void storeChanged(); // CalendarStore::changed 的窗口侧刷新入口

signals:
    void miniRequested();  // 用户点击「收起」（模块切到右上角迷你条）
    void dockChanged(bool docked); // 贴边收起状态（模块联动迷你条展示）

protected:
    void paintEvent(QPaintEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message,
                     qintptr *result) override;

private:
    enum class Edge { None, Left, Right, Top };

    // ---- UI 构建 ----
    void buildUi();
    void applyTheme();       // 由 bg/accent 推导的全套配色应用
    void refreshTitle();     // 月份标签

    // ---- 数据刷新 ----
    void refreshLists();
    void refreshDots();
    void loadItemRow(QListWidget *list, const CalendarItem &item);

    // ---- 交互 ----
    void addFromInput(QLineEdit *input, bool todo);
    void editItem(QListWidget *list, QListWidgetItem *row);
    void removeItem(QListWidget *list, QListWidgetItem *row);
    void toggleDone(QListWidget *list, QListWidgetItem *row);
    void toggleImportant(QListWidget *list, QListWidgetItem *row);
    void moveItemRelative(QListWidget *list, QListWidgetItem *row, int delta);
    void showContextMenu(QListWidget *list, bool todo, const QPoint &globalPos);
    void syncReorder(QListWidget *list, bool todo); // 拖拽落点后回写顺序

    // ---- 贴边收起 ----
    void maybeSnapToEdge();
    void dockToEdge(Edge edge);
    void expandFromEdge();
    Edge currentEdge() const;

    // ---- 几何/标志 ----
    void updateWindowFlags(); // 置顶开关（需要重新 show）

    CalendarStore *m_store;

    // 标题栏
    QToolButton *m_pinBtn = nullptr;
    QToolButton *m_miniBtn = nullptr;
    QToolButton *m_closeBtn = nullptr;

    // 月历列
    MonthView *m_monthView = nullptr;
    QLabel *m_monthLabel = nullptr;

    // 列表列
    QListWidget *m_scheduleList = nullptr;
    QListWidget *m_todoList = nullptr;
    QLineEdit *m_scheduleInput = nullptr;
    QLineEdit *m_todoInput = nullptr;
    QLabel *m_scheduleLabel = nullptr;
    QLabel *m_todoLabel = nullptr;
    QSplitter *m_hSplit = nullptr; // 左月历 | 右列表
    QSplitter *m_vSplit = nullptr; // 日程 / 待办

    // 主题
    QColor m_bg = QColor("#FFFFFF");
    QColor m_accent = QColor(0x2B, 0x6F, 0xF2);
    bool m_darkText = false;

    // 贴边收起
    class EdgeTab;
    EdgeTab *m_edgeTab = nullptr;
    QByteArray m_preDockGeometry;
    qint64 m_snapCooldownUntil = 0; // ms since epoch；展开后冷却防立即回吸
};

} // namespace poptools
