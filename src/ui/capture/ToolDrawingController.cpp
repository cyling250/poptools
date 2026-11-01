#include "ToolDrawingController.h"

namespace poptools {

void ToolDrawingController::setTool(Tool t)
{
    // 组装工具上下文（注入画板 / 屏幕 / 工具栏引用 + 窗口回调）
    m_toolCtx.canvas = m_ctx->canvas;
    m_toolCtx.cap = m_ctx->cap;
    m_toolCtx.toolbar = m_ctx->toolbar;
    m_toolCtx.selection = m_ctx->selectionRect(); // 工具激活时选区快照
    m_toolCtx.requestUpdate = m_ctx->requestUpdate;
    m_toolCtx.stateChanged = m_ctx->stateChanged;
    m_toolCtx.buildMosaic = [this](const QPolygonF &points, qreal width)
    {
        return buildMosaicItem(points, width);
    };
    m_toolCtx.beginText = [this](const QPointF &local, int existing)
    {
        m_ctx->beginTextEdit(local, existing);
    };
    m_tool = ToolFactory::create(t, &m_toolCtx);
    m_drawing = false;
}

Tool ToolDrawingController::tool() const
{
    return m_ctx->canvas ? m_ctx->canvas->tool : Tool::None;
}

void ToolDrawingController::start(const QPointF &local)
{
    if (!m_tool)
        return;
    m_drawing = true;
    m_tool->onPress(local);
    // 橡皮 / 文字无草稿交互，立即结束绘制态
    if (tool() == Tool::Eraser || tool() == Tool::Text)
        m_drawing = false;
    m_ctx->requestUpdate();
}

void ToolDrawingController::drag(const QPointF &local)
{
    if (!m_drawing || !m_tool)
        return;
    m_tool->onDrag(local); // 工具内部经 requestUpdate 触发重绘
    m_ctx->requestUpdate();
}

void ToolDrawingController::finish(const QPointF &local)
{
    if (!m_drawing || !m_tool)
        return;
    m_tool->onRelease(local);
    m_drawing = false;
    m_ctx->requestUpdate();
}

void ToolDrawingController::abortDraft()
{
    if (!m_drawing || !m_tool)
        return;
    // 仅丢弃草稿：工具实例保留（各工具 onPress 会重置草稿状态，
    // 见 ShapeTool/BrushTool），用于 ESC 取消当前笔画而不退出工具
    m_drawing = false;
    m_ctx->requestUpdate();
}

void ToolDrawingController::paintDraft(QPainter &p) const
{
    if (m_drawing && m_tool)
        m_tool->paintDraft(p);
}

Qt::CursorShape ToolDrawingController::cursor() const
{
    return m_tool ? m_tool->cursor() : Qt::SizeAllCursor;
}

// 马赛克笔刷：沿笔刷足迹（折线 + 圆头）像素化，足迹以外区域保持透明
AnnotationItem ToolDrawingController::buildMosaicItem(const QPolygonF &points, qreal width) const
{
    AnnotationItem draft;
    draft.type = AnnotationType::Mosaic;
    draft.width = width;
    if (points.isEmpty())
        return draft;

    const QRect selection = m_ctx->selectionRect();
    const CaptureResult *cap = m_ctx->cap;
    const qreal r = qMax(2.0, width / 2.0); // 笔刷半径
    // 足迹外接矩形（选区局部坐标），限制在选区内
    QRectF bounds;
    for (const QPointF &pt : points)
        bounds = bounds.united(QRectF(pt - QPointF(r, r), QSizeF(2 * r, 2 * r)));
    const QRect local = bounds.toAlignedRect()
                            .adjusted(-1, -1, 1, 1)
                            .intersected(QRect(QPoint(0, 0), selection.size()));
    if (local.isEmpty())
        return draft;

    // 截取屏幕源区域并整体像素化（m_cap.screen 为物理像素 → 换算物理矩形）
    const QRect global = local.translated(selection.topLeft()).intersected(cap->desktop);
    if (global.isEmpty())
        return draft;
    const QRect physGlobal(QRect(ScreenCapturer::toPhysical(global.topLeft()),
                                 ScreenCapturer::toPhysical(global.bottomRight() + QPoint(1, 1)) - QPoint(1, 1)));
    if (physGlobal.isEmpty())
        return draft;
    const QPixmap region = cap->screen.copy(physGlobal.translated(-cap->physDesktop.topLeft()));
    const int block = 8; // 马赛克块大小（物理像素）
    if (region.width() < block || region.height() < block)
        return draft;
    QImage src = region.toImage();
    QImage small = src.scaled(qMax(1, src.width() / block), qMax(1, src.height() / block),
                              Qt::IgnoreAspectRatio, Qt::FastTransformation);
    // 必须转成带 alpha 的格式再合成：RGB32 无 alpha 通道，DestinationIn 会按源
    // alpha 把 RGB 乘成 0（纯黑）且图像仍不透明 → 表现为"黑色填充矩形+透明路径"
    QImage mosaic = small.scaled(src.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation)
                        .convertToFormat(QImage::Format_ARGB32_Premultiplied);

    // 笔刷足迹 mask（物理像素，与 mosaic 同尺寸）：保留沿线的圆头笔刷区域（其余透明）
    QImage mask(physGlobal.size(), QImage::Format_ARGB32);
    mask.fill(Qt::transparent);
    {
        QPainter p(&mask);
        p.setRenderHint(QPainter::Antialiasing);
        // 选区局部逻辑坐标 → 物理像素
        p.scale(qreal(physGlobal.width()) / qMax(1, local.width()),
                qreal(physGlobal.height()) / qMax(1, local.height()));
        p.setPen(QPen(Qt::white, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::white);
        const QPointF off = QPointF(local.topLeft()); // 选区局部 → 区域局部
        QPolygonF pts;
        for (const QPointF &pt : points)
            pts << pt - off;
        p.drawPolyline(pts);
        // 单点 / 端点处补圆点，保证笔刷为圆头
        for (const QPointF &pt : pts)
            p.drawEllipse(pt, r, r);
    }
    {
        QPainter p(&mosaic);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.drawImage(0, 0, mask);
    }

    draft.mosaicImage = mosaic;
    draft.rect = QRectF(local);
    return draft;
}

} // namespace poptools
