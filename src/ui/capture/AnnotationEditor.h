#pragma once

#include <QPointF>
#include <QRectF>
#include <QPainter>
#include <QVector>

#include "core/annotation/AnnotationPainter.h"
#include "CaptureContext.h"

namespace poptools {

// ============================================================
// 标注项编辑器：选择模式下标注的选中 / 拖动 / 控制点缩放 / 删除
// + 文字标注双击重编辑 + 选中态绘制与光标反馈
//
// 交互坐标均为选区局部坐标（painter 已 translate）；选中索引
// 由 CaptureContext 共享（*selectedItem）。
// ============================================================
class AnnotationEditor
{
public:
    explicit AnnotationEditor(CaptureContext *ctx) : m_ctx(ctx) {}

    // 交互
    bool tryPress(const QPointF &local);      // 命中选中项控制点→缩放；点选→选中并拖动。返回是否处理
    // 拖动中（CaptureWindow::mouseMoveEvent 按此分派 drag）
    bool isMoving() const { return m_itemMoving; }
    // 缩放中（含选区控制点互斥判定，真值时 CaptureWindow 显示取色卡片）
    bool isResizing() const { return m_itemActiveHandle != Handle::None; }
    void drag(const QPointF &local);          // 按活动状态移动 / 缩放
    void endDrag();                           // 释放
    void stopInteraction();                   // 立即停止移动/缩放（文字编辑等入口调用）
    bool tryDoubleClickText(const QPointF &local); // 双击文字标注 → 启动重编辑（ctx->beginTextEdit）
    void deleteSelected();                    // 删除选中标注（压撤销栈）
    // 取消标注选中：清 *selectedItem 与拖动/缩放态，并触发 selectionChanged
    // （工具栏字号栏随之复位）。一切"切换上下文"的入口（切工具/点选区外/
    // 重进截图/双击重画）都应调用，否则旧选中态会串到新上下文
    void clearSelection();
    bool hasSelection() const { return m_ctx->selectedIndex() >= 0; }

    // 绘制 / 光标
    void paintSelected(QPainter &p) const;    // 选中项虚线框 + 控制点（painter 在选区局部坐标系）
    // 选中项上的光标形状：命中控制点 → 尺寸光标，命中项边界（±4px 容差）→
    // 移动光标，否则箭头
    Qt::CursorShape cursorAt(const QPointF &local) const;

private:
    QRectF handleRect(const AnnotationItem &it, Handle h) const;
    Handle handleAt(const AnnotationItem &it, const QPointF &pos) const;
    void move(const QPointF &local);
    void resize(const QPointF &local);
    void finalizeMosaicPreview(); // 马赛克缩放预览定稿（见 resize 实现）

    CaptureContext *m_ctx;

    bool m_itemMoving = false;                    // 正在拖动选中项
    Handle m_itemActiveHandle = Handle::None;     // 正在缩放选中项的控制点
    bool m_itemSnapshotted = false;               // 本次交互是否已压撤销快照
    QPointF m_itemStartPos;                       // 按下时的鼠标（局部坐标）
    QRectF m_itemStartBounds;                     // 按下时的标注外接矩形
    AnnotationItem m_itemStartGeom;               // 按下时的标注几何快照
    QPointF m_itemLastLocal;                      // 缩放中最后一次鼠标位置（定稿重算用）
    bool m_mosaicPreviewFrame = false;            // 马赛克缩放中间帧待定稿（R3 #24）
};

} // namespace poptools
