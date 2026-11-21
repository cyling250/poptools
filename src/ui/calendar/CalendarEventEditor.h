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
// 无边框圆角深色窗，多行文本一行一条，右上角 ✓ 保存，
// ESC / Ctrl+S / 失焦 即保存关闭。
// T-072（用户拍板）：原 rlqd 工具条五钮（插时间 + 四个提示占位）与
// "数据保存在本机"页脚撤除——图标语义不明、150% DPI 下显示不全；
// 弹出位置改对齐所点击日期格居中（见 CalendarPanel::openEditor）。
// ============================================================
class CalendarEventEditor : public QWidget
{
    Q_OBJECT
public:
    // 编辑器逻辑尺寸（S-07：字面量在面板定位处重复，收敛为此常量；
    // 面板按 dpr 折算成物理尺寸做钳制，见 CalendarPanel::openEditor）。
    // T-072：430 → 340，随工具条/页脚撤除收缩（标题 48 + 文本 280 + 边距）
    static constexpr QSize kEditorSize{380, 340};

    CalendarEventEditor(const QDate &date, CalendarTaskStore *store, const QPoint &pos,
                        QWidget *parent = nullptr);

    // 收集文本保存回任务库并关闭（公开：宿主的全局鼠标钩子兜底"点外即关"调用）
    void saveAndClose();

protected:
    // 全自绘：深色底 + 标题 + ✓（T-072：工具条与页脚已撤）
    void paintEvent(QPaintEvent *event) override;
    // ESC / Ctrl+S = 保存关闭（不丢弃）
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    // WindowDeactivate 失焦即保存关闭（m_closing 防递归）
    bool event(QEvent *event) override;

private:
    QDate m_date;
    CalendarTaskStore *m_store = nullptr;
    QPlainTextEdit *m_text = nullptr;
    QRect m_checkRect;
    bool m_closing = false;
};

} // namespace poptools
