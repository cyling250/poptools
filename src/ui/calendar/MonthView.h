#pragma once

#include <QColor>
#include <QDate>
#include <QHash>
#include <QWidget>
#include <QVector>

namespace poptools {

// ============================================================
// 月历视图（自绘）：公历日期 + 农历小字 + 节日色 + 事项圆点。
//
// 纯展示控件：不认识 CalendarStore（避免 ui→业务耦合），
// 由 CalendarWindow 喂入圆点数据与配色；点击选择日期。
// 周一为每周首日（中文月历惯例）。
// ============================================================
class MonthView : public QWidget
{
    Q_OBJECT
public:
    explicit MonthView(QWidget *parent = nullptr);

    void setSelectedDate(const QDate &date);
    QDate selectedDate() const { return m_selected; }

    // 当月各日期的事项圆点（每格最多画 3 个）
    void setEventDots(const QHash<QDate, QVector<QColor>> &dots);

    struct Palette
    {
        QColor bg;          // 格子底（=窗口背景）
        QColor text;        // 主文字（公历数字）
        QColor subText;     // 次级文字（农历小字）
        QColor dimText;     // 非当月日期
        QColor festival;    // 节日色
        QColor accent;      // 今天/选中
        QColor line;        // 网格线
    };
    void setViewPalette(const Palette &p);

signals:
    void selectedDateChanged(const QDate &date);
    void dayActivated(const QDate &date); // 双击（用于快速新增）

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    QSize minimumSizeHint() const override { return QSize(240, 210); }

private:
    int rowCount() const { return 6; }
    int colCount() const { return 7; }
    QRect cellRect(int row, int col) const;
    // 格子坐标 → 日期（不在有效格返回无效日期）
    QDate dateAt(const QPoint &pos) const;
    static int firstDayColumn(const QDate &firstOfMonth); // 周一=0

    QDate m_selected;
    Palette m_pal;
    QHash<QDate, QVector<QColor>> m_dots;
};

} // namespace poptools
