#pragma once

#include <QColor>
#include <QDate>
#include <QWidget>

class QLabel;
class QListWidget;
class QToolButton;

namespace poptools {

class CalendarStore;

// ============================================================
// 迷你待办条：主窗口收起后贴在屏幕右上角，只显示今日待办。
//
// 「最小化后收进屏幕右上角，只显示待办」——未完成置顶显示、
// 完成的打勾划线；点击条目左侧勾选区切换完成；双击或点「展开」
// 恢复完整日历；✕ 关闭；标题区拖动（HTCAPTION）。
// ============================================================
class CalendarMiniWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CalendarMiniWidget(CalendarStore *store, QWidget *parent = nullptr);

    void refresh();       // 数据/日期变化后刷新内容（模块连接 store 变更与分钟定时器）
    void applySettings(); // 透明度/颜色/置顶重应用

signals:
    void expandRequested(); // 恢复完整日历（模块负责显示主窗口）

protected:
    void paintEvent(QPaintEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message,
                     qintptr *result) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void buildUi();
    void applyTheme();

    CalendarStore *m_store;
    QLabel *m_dateLabel = nullptr;
    QListWidget *m_list = nullptr;
    QColor m_bg = QColor("#FFFFFF");
    QColor m_accent = QColor(0x2B, 0x6F, 0xF2);
    bool m_darkText = false;
    QDate m_loadedForDate;
};

} // namespace poptools
