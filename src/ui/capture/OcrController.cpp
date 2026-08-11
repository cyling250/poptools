#include "OcrController.h"

#include <QMessageBox>
#include <QFontMetricsF>
#include <QObject>

namespace snipaste {

OcrController::OcrController(CaptureContext *ctx)
    : m_ctx(ctx)
{
    // 连接识别完成信号（用 m_ocr 自身作 context 对象，避免本类继承 QObject）
    QObject::connect(&m_ocr, &OcrService::ocrBoxesFinished, &m_ocr,
                     [this](bool ok, const QVector<OcrBox> &boxes, const QString &error)
                     {
                         onBoxes(ok, boxes, error);
                     });
}

OcrController::~OcrController() = default;

void OcrController::request(QWidget *parent)
{
    if (m_ctx->selectionRect().isEmpty())
        return;
    if (m_ocr.busy())
        return; // 上次识别未结束，忽略重复点击
    if (!m_ocr.available())
    {
        QMessageBox::warning(parent, "文字识别",
                             "未找到 OCR 引擎 tesseract.exe，请确认其位于程序目录的 tesseract 文件夹中。");
        return;
    }

    // 裁剪选区（物理分辨率）交给 OCR；结果文本框为裁剪图像素坐标
    const QRect physCrop = ScreenCapturer::toPhysicalRect(m_ctx->selectionRect())
                               .translated(-m_ctx->cap->physDesktop.topLeft());
    if (physCrop.isEmpty())
        return;
    m_ocrImg = m_ctx->cap->screen.copy(physCrop).toImage();
    m_ocr.recognizeBoxes(m_ocrImg);
}

// 覆盖文字字号与原文字匹配：OCR 行框高度 ≈ 原文字实际绘制高度，
// 用 QFontMetricsF 实测迭代，使覆盖文字高度落在 [local.height()-1, local.height()] 区间
// （pixelSize 与字体高度近似线性）
QFont OcrController::fitFontHeight(const QRectF &local)
{
    QFont f(QStringLiteral("Microsoft YaHei"));
    f.setPixelSize(qBound(6, qRound(local.height() * 0.8), 200));
    QFontMetricsF fm(f);
    if (!qFuzzyIsNull(fm.height()))
    {
        // 比例修正一次即可逼近目标（字体高度 ∝ pixelSize）
        const int ps = qBound(6, qRound(f.pixelSize() * local.height() / fm.height()), 200);
        f.setPixelSize(ps);
        fm = QFontMetricsF(f);
    }
    // 精调 ±1px：不超过原文字高度，也不明显小于它
    for (int guard = 0; guard < 20; ++guard)
    {
        if (fm.height() > local.height() && f.pixelSize() > 6)
        {
            f.setPixelSize(f.pixelSize() - 1);
            fm = QFontMetricsF(f);
        }
        else if (fm.height() + 1.0 < local.height() && f.pixelSize() < 200)
        {
            f.setPixelSize(f.pixelSize() + 1);
            fm = QFontMetricsF(f);
        }
        else
        {
            break;
        }
    }
    return f;
}

void OcrController::onBoxes(bool ok, const QVector<OcrBox> &boxes, const QString &error)
{
    if (!ok)
    {
        QMessageBox::warning(nullptr, "文字识别",
                             error.isEmpty() ? QStringLiteral("未识别到文字") : error);
        return;
    }

    // 裁剪图像素坐标 → 选区局部逻辑坐标（按各轴缩放比例换算）
    const QRect selection = m_ctx->selectionRect();
    const int selW = qMax(1, selection.width());
    const int selH = qMax(1, selection.height());
    const qreal kx = qreal(m_ocrImg.width()) / selW;
    const qreal ky = qreal(m_ocrImg.height()) / selH;
    const QRectF localClip(0, 0, selW, selH);

    for (const OcrBox &box : boxes)
    {
        QRectF local(box.rect.x() / kx, box.rect.y() / ky,
                     box.rect.width() / kx, box.rect.height() / ky);
        local = local.intersected(localClip);
        if (local.isEmpty() || box.text.trimmed().isEmpty())
            continue;

        AnnotationItem it;
        it.type = AnnotationType::Text;
        it.color = m_ctx->canvas->color;
        it.width = m_ctx->canvas->width;
        it.rect = local;
        it.text = box.text;
        it.font = fitFontHeight(local);
        m_ctx->canvas->addItem(it);
    }
    m_ctx->stateChanged();
    m_ctx->requestUpdate();
}

} // namespace snipaste
