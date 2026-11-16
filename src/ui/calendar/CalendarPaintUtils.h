#pragma once

#include <QPainterPath>
#include <QRectF>

#include <algorithm> // std::min

namespace poptools {

// ============================================================
// ui/calendar 模块内共用的绘制小工具（S-03 收敛圆角矩形路径的三份同构副本：
// CalendarRenderer 的匿名 roundedPath / CalendarEventEditor 的 editorRoundPath /
// 已删除的 CalendarRenderer::roundRectPath 死代码，原各持一份逐字相同实现）。
// 跨模块（ui/shared、ui/box）的进一步收敛沿用 box-r1 C-03 登记，不在本批。
// ============================================================

// 圆角矩形路径：radius 超过矩形半边时自动钳制（与原实现逐字等价）
inline QPainterPath roundedPath(const QRectF &r, qreal radius)
{
    QPainterPath path;
    const qreal d = std::min(radius * 2, std::min(r.width(), r.height()));
    path.moveTo(r.left(), r.top() + d);
    path.arcTo(QRectF(r.left(), r.top(), d, d), 180, -90);
    path.lineTo(r.right() - d, r.top());
    path.arcTo(QRectF(r.right() - d, r.top(), d, d), 90, -90);
    path.lineTo(r.right(), r.bottom() - d);
    path.arcTo(QRectF(r.right() - d, r.bottom() - d, d, d), 0, -90);
    path.lineTo(r.left() + d, r.bottom());
    path.arcTo(QRectF(r.left(), r.bottom() - d, d, d), 270, -90);
    path.closeSubpath();
    return path;
}

} // namespace poptools
