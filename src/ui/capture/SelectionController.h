#pragma once

#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QPainter>

#include "core/annotation/AnnotationPainter.h"
#include "CaptureContext.h"

namespace poptools {

// ============================================================
// 选区控制器：框选 / 整体移动 / 控制点精调 + 选区几何与绘制
//
// 全部交互基于 CaptureContext 中的当前选区（全局逻辑坐标），
// 与窗口实现解耦。
// ============================================================
class SelectionController
{
public:
    explicit SelectionController(CaptureContext *ctx) : m_ctx(ctx) {}

    // ---- 几何（纯函数，静态） ----
    static QRectF handleRect(const QRectF &r, Handle h);         // 控制点方块（r 所在坐标系）
    static Handle handleAt(const QRectF &b, const QPointF &pos); // 控制点命中
    static QRectF resizeRectF(QRectF r, Handle h, const QPointF &pos, qreal minSize); // 按控制点调整矩形
    static Qt::CursorShape cursorShape(Handle h);                // 控制点对应光标

    // 当前选区（全局坐标）上的控制点方块 / 命中
    QRect handleRect(Handle h) const;
    Handle handleAt(const QPoint &global) const;

    // ---- 框选（Selecting 态） ----
    void beginSelect(const QPoint &global);
    void dragSelect(const QPoint &global);
    QRect finishSelect(const QPoint &global); // 返回最终选区（normalized）

    // ---- 编辑态：选区控制点精调 / 整体移动 ----
    bool tryHandlePress(const QPoint &global); // 命中选区控制点 → 开始精调，返回是否处理
    bool tryStartMove(const QPoint &global);   // 点选区内空白 → 开始整体移动，返回是否处理
    void dragActive(const QPoint &global);     // 拖动控制点或整体移动（按当前活动状态）
    void cancelActive();                       // 释放 / 取消：清活动状态
    bool isActive() const { return m_activeHandle != Handle::None || m_moving; }
    bool resizing() const { return m_activeHandle != Handle::None; } // 正在调整选区控制点

    // ---- 绘制 ----
    // 蒙版 + 框选虚线框；highlightRect = 需在蒙版中挖空的区域（智能框选预高亮，窗口坐标，可空）
    void paintMaskAndFrame(QPainter &p, const QRect &windowRect, bool selecting,
                           const QRect &highlightRect = QRect()) const;
    void paintSelectionFrame(QPainter &p, const QRect &sel) const; // 蓝色边框 + 控制点 + 尺寸提示

private:
    CaptureContext *m_ctx;

    QPoint m_origin;              // 框选起点（全局坐标）
    // 控制点精调
    Handle m_activeHandle = Handle::None;
    QPoint m_handleStartPos;
    QRect m_handleStartSel;
    // 整体移动
    bool m_moving = false;
    QPoint m_moveOffset;
};

} // namespace poptools
