#include "OutputController.h"

#include <QPainter>
#include <QFileDialog>
#include <QClipboard>
#include <QGuiApplication>
#include <QStandardPaths>

#include "core/annotation/AnnotationPainter.h"

namespace snipaste {

// 输出为物理分辨率：逻辑选区 → 物理矩形裁剪；标注为选区局部逻辑坐标，
// 通过 scale(k,k) 等比放大（k = 物理宽 / 逻辑宽）。
QPixmap OutputController::render() const
{
    const QRect crop = ScreenCapturer::toPhysicalRect(m_ctx->selectionRect())
                           .translated(-m_ctx->cap->physDesktop.topLeft());
    QPixmap result = m_ctx->cap->screen.copy(crop);
    {
        QPainter p(&result);
        const qreal k = qreal(result.width()) / qMax(1, m_ctx->selectionRect().width());
        p.scale(k, k);
        AnnotationPainter::draw(p, m_ctx->canvas->items);
        if (m_ctx->hasDraft && m_ctx->hasDraft())
            m_ctx->paintDraft(p);
    }
    return result;
}

void OutputController::save(QWidget *parent)
{
    const QPixmap result = render();
    const QString path = QFileDialog::getSaveFileName(
        parent, "保存截图", QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        "PNG 图片 (*.png);;JPEG 图片 (*.jpg)");
    if (path.isEmpty())
        return; // 用户取消对话框 → 留在编辑界面
    result.save(path);
    m_ctx->exitRequested(); // 保存后退出截图界面（显式操作才退出，防误触）
}

void OutputController::copy()
{
    QGuiApplication::clipboard()->setPixmap(render());
    m_ctx->exitRequested(); // 复制后退出截图界面（显式操作才退出，防误触）
}

} // namespace snipaste
