#include "OcrController.h"

#include <QMessageBox>
#include <QFontMetricsF>
#include <QObject>
#include <algorithm>

#include "ToolBar.h"

namespace snipaste {

OcrController::OcrController(CaptureContext *ctx)
    : m_ctx(ctx)
{
    // 连接识别完成信号（用 m_ocr 自身作 context 对象，避免本类继承 QObject）
    QObject::connect(&m_ocr, &OcrService::ocrBoxesFinished, &m_ocr,
                     [this](bool ok, const QVector<OcrBox> &boxes, const QString &error)
                     {
                         if (m_translateMode)
                             onBoxesTranslate(ok, boxes, error);
                         else
                             onBoxes(ok, boxes, error);
                     });
}

OcrController::~OcrController() = default;

void OcrController::request(QWidget *parent)
{
    m_translateMode = false;
    if (!prepareAndRecognize(parent))
        return;
}

void OcrController::requestTranslate(QWidget *parent)
{
    m_translateMode = true;
    if (!prepareAndRecognize(parent))
        return;
    // 面板模式：先显示原文（此时尚未有识别结果，等 onBoxesTranslate 填原文）
    if (m_ctx->showOcrPanel)
        m_ctx->showOcrPanel(QString());
}

// 公共准备：检查状态 + 裁剪选区图像 + 启动识别；不可用时返回 false
bool OcrController::prepareAndRecognize(QWidget *parent)
{
    m_parent = parent; // 保存父窗口供 onBoxes/onBoxesTranslate 中 QMessageBox 使用，避免被置顶窗口遮挡
    if (m_ctx->selectionRect().isEmpty())
        return false;
    if (m_ocr.busy())
        return false; // 上次识别未结束，忽略重复点击
    if (!m_ocr.available())
    {
        if (m_ctx->ocrBusy)
            m_ctx->ocrBusy(false); // 确保结束态（防止 start 未调用）
        QMessageBox::warning(parent, "文字识别",
                             "未找到 OCR 引擎（libtesseract-5.dll），请确认其位于程序目录的 tesseract 文件夹中。");
        return false;
    }

    // 通知扫描动效开始
    if (m_ctx->ocrBusy)
        m_ctx->ocrBusy(true);

    // 裁剪选区（物理分辨率）交给 OCR；结果文本框为裁剪图像素坐标
    const QRect physCrop = ScreenCapturer::toPhysicalRect(m_ctx->selectionRect())
                               .translated(-m_ctx->cap->physDesktop.topLeft());
    if (physCrop.isEmpty())
        return false;
    m_ocrImg = m_ctx->cap->screen.copy(physCrop).toImage();
    m_ocr.recognizeBoxes(m_ocrImg);
    return true;
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
    // 通知扫描动效结束
    if (m_ctx->ocrBusy)
        m_ctx->ocrBusy(false);

    if (!ok)
    {
        QMessageBox::warning(m_parent, "文字识别",
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

// 【译】模式：把识别到的文字送入文字栏面板，并自动发起翻译
void OcrController::onBoxesTranslate(bool ok, const QVector<OcrBox> &boxes, const QString &error)
{
    // 通知扫描动效结束
    if (m_ctx->ocrBusy)
        m_ctx->ocrBusy(false);

    if (!ok)
    {
        // 识别失败：解除面板忙状态，避免翻译按钮一直"翻译中…"
        if (m_ctx->setPanelBusy)
            m_ctx->setPanelBusy(false);
        QMessageBox::warning(m_parent, "文字识别",
                             error.isEmpty() ? QStringLiteral("未识别到文字") : error);
        return;
    }

    // 按阅读顺序（先从上到下，再从左到右）合并行文本
    QVector<OcrBox> sorted = boxes;
    std::sort(sorted.begin(), sorted.end(), [](const OcrBox &a, const OcrBox &b)
              {
                  if (a.rect.y() != b.rect.y())
                      return a.rect.y() < b.rect.y();
                  return a.rect.x() < b.rect.x();
              });
    QString text;
    for (const OcrBox &box : sorted)
    {
        const QString t = box.text.trimmed();
        if (t.isEmpty())
            continue;
        if (!text.isEmpty())
            text += '\n';
        text += t;
    }
    m_lastText = text;

    // 显示原文 + 自动翻译（目标语言取面板当前选择）
    if (m_ctx->showOcrPanel)
        m_ctx->showOcrPanel(text);
    if (m_ctx->translateRequested && !text.isEmpty())
        m_ctx->translateRequested(text);
}

} // namespace snipaste
