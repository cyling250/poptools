#include "OutputController.h"

#include <QPainter>
#include <QFileDialog>
#include <QClipboard>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QMessageBox>
#include <QDateTime>

#include "core/annotation/AnnotationPainter.h"

namespace poptools {

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
    // 只提供 PNG（无损）格式：截图必须无损画质，不落 JPEG 等有损压缩格式
    const QString defName = QStringLiteral("PopTools_%1.png")
                                .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    const QString path = QFileDialog::getSaveFileName(
        parent, "保存截图",
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/" + defName,
        "PNG 图片（无损） (*.png)");
    if (path.isEmpty())
        return; // 用户取消对话框 → 留在编辑界面
    if (!result.save(path)) {
        QMessageBox::warning(parent, "保存失败", "无法保存截图到所选路径。\n\n路径: " + path);
        return; // 保存失败，留在编辑界面
    }
    m_ctx->exitRequested(); // 保存后退出截图界面（显式操作才退出，防误触）
}

void OutputController::copy()
{
    QGuiApplication::clipboard()->setPixmap(render());
    m_ctx->exitRequested(); // 复制后退出截图界面（显式操作才退出，防误触）
}

} // namespace poptools
