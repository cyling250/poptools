#pragma once

#include <QColor>
#include <QWidget>

#include <functional>
#include <vector>

namespace poptools {

struct CalendarMenuItemDef
{
    QString text;
    std::function<void()> action;
    bool separatorAfter = false;
    bool showCheck = false;
    bool checked = false;
};

// ============================================================
// 主菜单弹层（照搬 rlqd CalendarClone/MenuForm.cs：自绘无边框窗口，
// 悬停高亮 / 分隔线 / 勾选项，失焦即关）。
// ============================================================
class CalendarMenuForm : public QWidget
{
    Q_OBJECT
public:
    explicit CalendarMenuForm(std::vector<CalendarMenuItemDef> items, const QPoint &pos,
                              QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseLeaveEvent(QEvent *event);
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;

private:
    int hit(const QPoint &p) const;

    std::vector<CalendarMenuItemDef> m_items;
    std::vector<QRect> m_rects;
    int m_hover = -1;
};

} // namespace poptools
