#pragma once

#include <QWidget>
#include <QToolButton>
#include <QColor>
#include <QVector>

#include "AnnotationCanvas.h"

// ============================================================
// 标注工具栏（编辑态底部显示）
//
// 布局（从左到右，对齐 DS 对齐信息）：
//   [绘制]  矩形  椭圆  箭头  折线
//   [编辑]  画笔  橡皮  文本  取色
//   [颜色]  预设色块面板（点击切换当前颜色）
//   [操作]  撤销  重做  |  保存  复制  完成
// ============================================================
class ToolBar : public QWidget
{
    Q_OBJECT
public:
    explicit ToolBar(QWidget *parent = nullptr);

    void setTool(AnnotationCanvas::Tool t);      // 同步外部工具状态
    void setColor(const QColor &c);              // 同步外部颜色状态
    void setUndoRedoEnabled(bool undo, bool redo);

signals:
    void toolSelected(AnnotationCanvas::Tool t);
    void colorSelected(const QColor &c);
    void undoRequested();
    void redoRequested();
    void saveRequested();
    void copyRequested();
    void confirmRequested();

private slots:
    void onToolClicked();
    void onColorClicked();

private:
    QToolButton *makeToolButton(const QString &label);
    QColor m_currentColor = Qt::red;
    QVector<QToolButton *> m_toolButtons;  // 工具按钮（含操作按钮）
    QVector<QToolButton *> m_colorButtons; // 色块按钮
};
