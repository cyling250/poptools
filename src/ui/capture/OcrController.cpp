#include "OcrController.h"

#include <QMessageBox>
#include <QFontMetricsF>
#include <QObject>
#include <algorithm>

#include "ToolBar.h"

namespace poptools {

OcrController::OcrController(CaptureContext *ctx, PaddleOcrService *ocr, OcrHostCallbacks host)
    : QObject(nullptr) // 无 Qt 父子所有权：由 CaptureWindow 的 unique_ptr 独占管理
    , m_ctx(ctx)
    , m_ocr(ocr)
    , m_host(std::move(host))
{
    // 连接识别完成信号（context 对象用自身：共享 OCR 服务寿命长于本控制器，
    // 必须随本控制器析构自动断连，避免悬垂 this）
    QObject::connect(m_ocr, &PaddleOcrService::ocrBoxesFinished, this,
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
    if (m_host.showPanel)
        m_host.showPanel(QString());
}

// 公共准备：检查状态 + 裁剪选区图像 + 启动识别；不可用时返回 false
bool OcrController::prepareAndRecognize(QWidget *parent)
{
    m_parent = parent; // 保存父窗口供 onBoxes/onBoxesTranslate 中 QMessageBox 使用，避免被置顶窗口遮挡
    if (m_ctx->selectionRect().isEmpty())
        return false;
    if (m_ocr->busy())
        return false; // 上次识别未结束，忽略重复点击
    if (!m_ocr->available())
    {
        if (m_host.ocrBusy)
            m_host.ocrBusy(false); // 确保结束态（防止 start 未调用）
        QMessageBox::warning(parent, "文字识别",
                             "未找到 OCR 引擎（PaddleOCR-json.exe），请确认其位于程序目录的 paddleocr 文件夹中。");
        return false;
    }

    // 通知扫描动效开始
    if (m_host.ocrBusy)
        m_host.ocrBusy(true);

    // 裁剪选区（物理分辨率）交给 OCR；结果文本框为裁剪图像素坐标
    const QRect physCrop = ScreenCapturer::toPhysicalRect(m_ctx->selectionRect())
                               .translated(-m_ctx->cap->physDesktop.topLeft());
    if (physCrop.isEmpty())
        return false;
    m_ocrImg = m_ctx->cap->screen.copy(physCrop).toImage();
    m_ocr->recognizeBoxes(m_ocrImg);
    return true;
}

// OCR 原位拟合字号：用 tightBoundingRect 实测文字墨迹高度（字体行高含
// ascent+descent，远大于字形实际高度，按行高拟合会使文字明显小于识别框），
// 迭代使墨迹高度逼近框高；最终覆盖由 AnnotationPainter 的仿射映射保证
QFont OcrController::fitFontHeight(const QRectF &local, const QString &text)
{
    QFont f(QStringLiteral("Microsoft YaHei"));
    f.setPixelSize(qBound(6, qRound(local.height() * 0.8), 200));
    QFontMetricsF fm(f);
    const qreal inkH = fm.tightBoundingRect(text).height();
    if (inkH > 0)
    {
        // 墨迹高度 ∝ pixelSize，比例修正一次即可逼近目标
        f.setPixelSize(qBound(6, qRound(f.pixelSize() * local.height() / inkH), 200));
        fm = QFontMetricsF(f);
    }
    // 精调 ±1px：不超过原文字高度，也不明显小于它
    for (int guard = 0; guard < 20; ++guard)
    {
        const qreal h = fm.tightBoundingRect(text).height();
        if (h > local.height() && f.pixelSize() > 6)
        {
            f.setPixelSize(f.pixelSize() - 1);
            fm = QFontMetricsF(f);
        }
        else if (h + 1.0 < local.height() && f.pixelSize() < 200)
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
    if (m_host.ocrBusy)
        m_host.ocrBusy(false);

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
        it.font = fitFontHeight(local, box.text);
        it.inPlaceFit = true;
        m_ctx->canvas->addItem(it);
    }
    m_ctx->stateChanged();
    m_ctx->requestUpdate();
}

// 【译】模式：把识别到的文字送入文字栏面板，并自动发起翻译
void OcrController::onBoxesTranslate(bool ok, const QVector<OcrBox> &boxes, const QString &error)
{
    // 通知扫描动效结束
    if (m_host.ocrBusy)
        m_host.ocrBusy(false);

    if (!ok)
    {
        // 识别失败：解除面板忙状态，避免翻译按钮一直"翻译中…"
        if (m_host.setPanelBusy)
            m_host.setPanelBusy(false);
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
    if (m_host.showPanel)
        m_host.showPanel(text);
    if (m_host.translateRequested && !text.isEmpty())
        m_host.translateRequested(text);
}

} // namespace poptools
