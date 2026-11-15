#pragma once

#include <QDate>
#include <QWidget>

class QMouseEvent;
class QPlainTextEdit;

namespace poptools {

class CalendarTaskStore;

// ============================================================
// 当日日程编辑器（照搬 rlqd CalendarClone/EventEditorForm.cs）：
// 无边框圆角深色窗，多行文本一行一条，右上角 ✓ 保存，工具条五钮
//（插入时间/重复/提醒/地点/颜色——后四项与 rlqd 同为提示占位），
// ESC / Ctrl+S / 失焦 即保存关闭。
// ============================================================
class CalendarEventEditor : public QWidget
{
    Q_OBJECT
public:
    CalendarEventEditor(const QDate &date, CalendarTaskStore *store, const QPoint &pos,
                        QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;

private:
    int hitTool(const QPoint &p) const;
    void handleTool(int index);
    void insertTime();
    void saveAndClose();
    void drawToolIcon(QPainter *g, int index, const QRect &r);

    QDate m_date;
    CalendarTaskStore *m_store = nullptr;
    QPlainTextEdit *m_text = nullptr;
    QRect m_toolRects[5];
    QRect m_checkRect;
    int m_hoverTool = -1;
    bool m_closing = false;
};

} // namespace poptools
