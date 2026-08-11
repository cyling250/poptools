#pragma once

#include <QImage>
#include <QVector>

#include "core/ocr/OcrService.h"
#include "CaptureContext.h"

namespace snipaste {

// ============================================================
// OCR 控制器：把选区内图像（物理分辨率）交给本地 Tesseract，
// 结果文本框映射回选区局部坐标并作为文字标注添加到画板。
// ============================================================
class OcrController
{
public:
    explicit OcrController(CaptureContext *ctx); // 构造时连接 OcrService 完成信号
    ~OcrController();

    bool busy() const { return m_ocr.busy(); }
    bool available() const { return m_ocr.available(); }

    void request(QWidget *parent); // 检查状态 + 裁剪选区图像 + 启动识别

private:
    void onBoxes(bool ok, const QVector<OcrBox> &boxes, const QString &error);
    static QFont fitFontHeight(const QRectF &local); // 覆盖文字字号与原文字匹配（实测逼近）

    CaptureContext *m_ctx;
    OcrService m_ocr;
    QImage m_ocrImg; // 传给 OCR 的选区裁剪图（物理像素）
};

} // namespace snipaste
