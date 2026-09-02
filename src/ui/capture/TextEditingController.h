#pragma once

#include <QPointF>
#include <QColor>
#include <QFont>
#include <QWidget>

#include "core/annotation/AnnotationItem.h"
#include "CaptureContext.h"

class QTextEdit;
class QTextCharFormat;

namespace poptools {

// ============================================================
// 文字内嵌编辑控制器
//
// 负责文字工具的全部输入交互：多行输入框（透明底虚线框、
// 尺寸随内容自适应）、富文本（多字号/多颜色）的创建与重编辑、
// 字号栏/颜色栏与输入内容的实时联动。
//
// 输入框定位依赖 CaptureContext 的选区与桌面几何；
// 提交/删除/改格式通过 canvas / toolbar / 回调完成。
// ============================================================
class TextEditingController
{
public:
    explicit TextEditingController(CaptureContext *ctx) : m_ctx(ctx) {}

    void init(QWidget *parent);       // 创建输入框并连接信号（构造后调用一次）

    bool isEditing() const { return m_editing; }
    int editingItem() const { return m_editItem; } // 正在编辑的标注索引（-1 新建）

    void begin(const QPointF &local, int itemIndex); // -1 新建，否则重编辑
    void commit();                   // 空输入删除 / 有输入保存
    void cancelByEsc();              // ESC：清空后提交（删除待输入框 / 标注）
    void onFontSizeSelected(qreal pt); // 字号提交：输入态/选中文字标注/无选中 三态分发
    void applyColor(const QColor &c);// 颜色提交（输入态）：语义与字号一致
    void syncToolbar();              // 字号栏反映当前选中/编辑文本的大小
    void detach();                   // 退出截图时清理状态

private:
    void adjustSize();                                  // 输入框尺寸自适应
    void applyFontSizeToInput(qreal pt);                // 输入态改字号（有选区改选中，无选区设后续输入）
    void applyFontSizeToItem(AnnotationItem &it, qreal pt); // 整条标注改统一字号
    qreal fragmentPt(const QTextCharFormat &fmt, const QFont &def) const; // 片段字号（pt）

    CaptureContext *m_ctx;
    QTextEdit *m_textEdit = nullptr;
    bool m_editing = false;
    int m_editItem = -1;
    QPointF m_editPos;              // 新建文本的选区局部坐标
    QColor m_startColor;            // 本次输入开始色（无显式色片段渲染兜底）
    qreal m_defaultFontSize = 14.0; // 无文本选中时的默认字号（接下来输入）
};

} // namespace poptools
