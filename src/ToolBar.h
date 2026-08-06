#pragma once

#include <QWidget>
#include <QToolButton>
#include <QColor>
#include <QVector>

#include "AnnotationCanvas.h"

// ============================================================
// 标注工具栏（编辑态，贴画布下方靠右显示，两行紧凑布局）
//
// 第一行（工具，互斥高亮）：
//   矩形 椭圆 直线 箭头 铅笔 涂鸦 马赛克 文字 橡皮 | 撤销 重做 | 保存 复制
// 第二行（属性）：
//   小线宽 中线宽 大线宽 | 填充/纯线 | 颜色板(两行八列) + 当前色大色块
//
// 高亮规则：第一类工具互斥，点击高亮（蓝底白图标）进入编辑模式；
// 再次点击当前高亮工具则取消（进入选择模式可拖动画布）。
// ============================================================
class ToolBar : public QWidget
{
    Q_OBJECT
public:
    explicit ToolBar(QWidget *parent = nullptr);

    void setTool(AnnotationCanvas::Tool t);      // 同步外部工具状态（含 None 选择模式）
    void setColor(const QColor &c);              // 同步外部颜色状态
    void setWidth(qreal w);                      // 同步线宽
    void setFilled(bool filled);                 // 同步填充状态
    void setUndoRedoEnabled(bool undo, bool redo);

    // 当前选中工具
    AnnotationCanvas::Tool currentTool() const { return m_tool; }
    // 当前颜色
    QColor currentColor() const { return m_currentColor; }

signals:
    void toolSelected(AnnotationCanvas::Tool t); // None = 选择模式
    void colorSelected(const QColor &c);
    void widthSelected(qreal w);
    void filledToggled(bool filled);
    void undoRequested();
    void redoRequested();
    void saveRequested();
    void copyRequested();
    void confirmRequested();

private slots:
    void onToolClicked();
    void onColorClicked();
    void onWidthClicked();
    void onFilledClicked();

private:
    QToolButton *makeToolButton(const QString &iconName);
    QToolButton *makeActionButton(const QString &iconName, const QString &tip);

    // 根据当前主题返回图标颜色（深色主题用浅色图标）
    QColor iconColor() const;
    // 按当前工具刷新线宽档位按钮（值 / 图标 / 选中态），并同步线宽
    void applyWidthOptions();

    AnnotationCanvas::Tool m_tool = AnnotationCanvas::Tool::None;
    QColor m_currentColor = Qt::red;
    qreal m_width = 2.0;
    int m_widthIndex = 1;      // 线宽档位（0 小 / 1 中 / 2 大），切换工具时保持相对档位
    bool m_filled = false;
    bool m_dark = false; // 系统深色主题

    QVector<QToolButton *> m_toolButtons;  // 第一类工具按钮（互斥）
    QVector<QToolButton *> m_colorButtons; // 色块按钮
    QVector<QToolButton *> m_widthButtons; // 线宽按钮
    QToolButton *m_fillBtn = nullptr;      // 填充/纯线切换
    QToolButton *m_colorBig = nullptr;     // 当前颜色大色块
    QToolButton *m_undoBtn = nullptr;      // 撤销
    QToolButton *m_redoBtn = nullptr;      // 重做
};
