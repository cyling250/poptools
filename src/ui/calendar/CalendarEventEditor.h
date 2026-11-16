#pragma once

#include <QDate>
#include <QSize>
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
    // 编辑器逻辑尺寸（S-07：原 380/430 字面量在面板定位处重复，收敛为此常量；
    // 面板按 dpr 折算成物理尺寸做钳制，见 CalendarPanel::openEditor）
    static constexpr QSize kEditorSize{380, 430};

    CalendarEventEditor(const QDate &date, CalendarTaskStore *store, const QPoint &pos,
                        QWidget *parent = nullptr);

    // 收集文本保存回任务库并关闭（公开：宿主的全局鼠标钩子兜底"点外即关"调用）
    void saveAndClose();

protected:
    // 全自绘：深色底 + 标题 + ✓ + 工具条 + 页脚提示（工具条矩形在画时回填
    // m_toolRects，供命中测试）
    void paintEvent(QPaintEvent *event) override;
    // ESC / Ctrl+S = 保存关闭（不丢弃）
    void keyPressEvent(QKeyEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    // WindowDeactivate 失焦即保存关闭（m_closing 防递归）
    bool event(QEvent *event) override;

private:
    // 返回命中的工具钮下标，-1 = 无
    int hitTool(const QPoint &p) const;
    // 工具条分发：0 插时间，1-4 提示占位
    void handleTool(int index);
    void insertTime();
    // 矢量小图标（时钟/循环/铃/定位/调色盘）
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
