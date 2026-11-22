#pragma once

#include <QDate>
#include <QSize>
#include <QString>
#include <QWidget>

class QMouseEvent;
class QPlainTextEdit;

namespace poptools {

class CalendarTaskStore;

// 摘要以 txt 弹出（系统 notepad，用户指定形态）：md 原文写 %TEMP% 副本后
// startDetached（副本归 notepad 所有，PopTools 不清理）。2026-11-22 从
// CalendarPanel 提升为域内共享自由函数：月报链接（面板）与日报链接（编辑器
// 标题行，用户拍板"点开之后显示在编辑器、点击直接弹记事本"）共用。
void openDigestInNotepad(const QString &path);

// ============================================================
// 当日日程编辑器（照搬 rlqd CalendarClone/EventEditorForm.cs）：
// 无边框圆角深色窗，多行文本一行一条，右上角 ✓ 保存，
// ESC / Ctrl+S / 失焦 即保存关闭。
// T-072（用户拍板）：原 rlqd 工具条五钮（插时间 + 四个提示占位）与
// "数据保存在本机"页脚撤除——图标语义不明、150% DPI 下显示不全；
// 弹出位置改对齐所点击日期格居中（见 CalendarPanel::openEditor）。
// 2026-11-22（用户拍板）：当日有 Goody 日报时标题行右侧显示「摘要»」链接
//（格内链接撤除，入口改此处），点击直接 notepad 弹出；notepad 抢焦点后
// 编辑器走失焦保存关闭，摘要在新窗口独立展示。
// ============================================================
class CalendarEventEditor : public QWidget
{
    Q_OBJECT
public:
    // 编辑器逻辑尺寸（S-07：字面量在面板定位处重复，收敛为此常量；
    // 面板按 dpr 折算成物理尺寸做钳制，见 CalendarPanel::openEditor）。
    // T-072：430 → 340，随工具条/页脚撤除收缩（标题 48 + 文本 280 + 边距）
    static constexpr QSize kEditorSize{380, 340};

    // digestPath：当日 Goody 日报 md 路径（空 = 无日报，不画链接）
    CalendarEventEditor(const QDate &date, CalendarTaskStore *store, const QPoint &pos,
                        const QString &digestPath = QString(), QWidget *parent = nullptr);

    // 收集文本保存回任务库并关闭（公开：宿主的全局鼠标钩子兜底"点外即关"调用）
    void saveAndClose();

protected:
    // 全自绘：深色底 + 标题 + 摘要链接（有日报时）+ ✓（T-072：工具条与页脚已撤）
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
    QString m_digestPath;  // 当日日报 md 路径（空 = 不画链接）
    QRect m_digestRect;
    QRect m_checkRect;
    bool m_closing = false;
};

} // namespace poptools
