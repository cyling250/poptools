#pragma once

#include <QWidget>
#include <QToolButton>
#include <QColor>
#include <QVector>

#include "core/annotation/AnnotationTypes.h"

class QComboBox;
class QFrame;

namespace poptools {

// ============================================================
// 标注工具栏（编辑态，贴画布下方靠右显示，两行紧凑布局）
//
// 第一行（工具，互斥高亮）：
//   矩形 椭圆 直线 箭头 铅笔 涂鸦 马赛克 文字 [扫描OCR] 译 橡皮 | 撤销 重做 | 保存 复制
// 第二行（属性）：
//   小线宽 中线宽 大线宽 | 填充/纯线 | 字号(多选+输入) | 颜色板(2×8) + 当前色大色块
//
// 高亮规则：第一类工具互斥，点击高亮（蓝底白图标）进入编辑模式；
// 再次点击当前高亮工具则取消（进入选择模式可拖动画布）。
//
// 信号语义：*Selected/*Requested 仅由用户操作触发；setTool/setWidth 等
// 程序性同步只刷新显示、不回射信号（宿主同步画布宽度用 currentWidth()）。
// ============================================================
class ToolBar : public QWidget
{
    Q_OBJECT
public:
    explicit ToolBar(QWidget *parent = nullptr);
    ~ToolBar() override;

    // 单例模式：整个应用仅一个工具栏实例
    // create() 由宿主窗口（CaptureWindow）首次创建并指定 parent；
    // instance() 供其它模块获取（未创建时返回 nullptr）
    static ToolBar *create(QWidget *parent);
    static ToolBar *instance();

    void setTool(Tool t);                      // 同步外部工具状态（含 None 选择模式）
    void setColor(const QColor &c);            // 同步外部颜色状态
    void setWidth(qreal w);                    // 同步线宽
    void setFilled(bool filled);               // 同步填充状态
    void setUndoRedoEnabled(bool undo, bool redo);
    void setFontSize(qreal pt);                // 字号栏显示统一字号（pt）
    void clearFontSize();                      // 字号栏置空（多字号混排，不显示）

    // 当前选中工具
    Tool currentTool() const { return m_tool; }
    // 当前颜色
    QColor currentColor() const { return m_currentColor; }
    // 当前线宽（随工具的线宽档位表变化；切换工具后由宿主经此同步画布）
    qreal currentWidth() const { return m_width; }
    // 当前字号（pt）
    qreal fontSize() const { return m_fontSize; }

signals:
    void toolSelected(Tool t);                 // None = 选择模式
    void colorSelected(const QColor &c);
    void widthSelected(qreal w);
    void filledToggled(bool filled);
    void fontSizeSelected(qreal pt);           // 字号提交（下拉选 / 回车 / 失焦），已通过合法性验证
    void undoRequested();
    void redoRequested();
    void saveRequested();
    void copyRequested();
    void ocrRequested();                       // 扫描按钮：OCR 结果以文本框覆盖到画布，不打开面板
    void translateRequested();                 // 【译】：OCR + 翻译，自动打开面板显示原文与翻译结果

private slots:
    void onToolClicked();
    void onColorClicked();
    void onWidthClicked();
    void onFilledClicked();
    void onFontSizeCommitted();                // 字号提交（验证合法才 emit）

private:
    QFrame *makeSeparator();                   // 行内竖分隔线（跟随主题色）

    // 根据当前主题返回图标颜色（深色主题用浅色图标）
    QColor iconColor() const;
    // 按当前工具刷新线宽档位按钮（值 / 图标 / 选中态），并同步线宽
    void applyWidthOptions();
    // 刷新字号栏显示（m_updatingFontSize 保护下，程序性变更不发射信号）
    void syncFontSizeDisplay();

    Tool m_tool = Tool::None;
    QColor m_currentColor = Qt::red;
    qreal m_width = 2.0;
    int m_widthIndex = 1;      // 线宽档位（0 小 / 1 中 / 2 大），切换工具时保持相对档位
    bool m_filled = false;
    bool m_dark = false; // 系统深色主题

    qreal m_fontSize = 14.0;   // 当前字号（pt）
    bool m_fontSizeMixed = false; // 选中文本存在多个字号 → 字号栏显示为空
    bool m_updatingFontSize = false; // 程序性刷新字号栏时的信号保护
    QComboBox *m_fontSizeBox = nullptr;    // 字号多选+输入框

private:
    QVector<QToolButton *> m_toolButtons;  // 第一类工具按钮（互斥）
    QVector<QToolButton *> m_colorButtons; // 色块按钮
    QVector<QToolButton *> m_widthButtons; // 线宽按钮
    QToolButton *m_fillBtn = nullptr;      // 填充/纯线切换
    QToolButton *m_colorBig = nullptr;     // 当前颜色大色块
    QToolButton *m_undoBtn = nullptr;      // 撤销
    QToolButton *m_redoBtn = nullptr;      // 重做

    static ToolBar *s_instance;            // 单例实例
};

} // namespace poptools
