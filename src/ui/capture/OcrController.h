#pragma once

#include <QImage>
#include <QString>
#include <QVector>

#include "core/ocr/PaddleOcrService.h"
#include "CaptureContext.h"

namespace snipaste {

// ============================================================
// OCR 控制器：把选区内图像（物理分辨率）交给 PaddleOCR，
// 结果文本框映射回选区局部坐标并作为文字标注添加到画板。
// ============================================================
class OcrController
{
public:
    explicit OcrController(CaptureContext *ctx); // 构造时连接 PaddleOcrService 完成信号
    ~OcrController();

    bool busy() const { return m_ocr.busy(); }
    bool available() const { return m_ocr.available(); }

    void request(QWidget *parent); // 检查状态 + 裁剪选区图像 + 启动识别
    void requestTranslate(QWidget *parent); // 识别并翻译：结果显示到文字栏面板（原始+翻译）

private:
    bool prepareAndRecognize(QWidget *parent); // 公共准备（检查状态 + 裁剪 + 启动识别）；不可用返回 false
    void onBoxes(bool ok, const QVector<OcrBox> &boxes, const QString &error);
    void onBoxesTranslate(bool ok, const QVector<OcrBox> &boxes, const QString &error);
    static QFont fitFontHeight(const QRectF &local, const QString &text); // 覆盖文字字号与原文字墨迹高度匹配（实测逼近）

    CaptureContext *m_ctx;
    PaddleOcrService m_ocr;
    QImage m_ocrImg; // 传给 OCR 的选区裁剪图（物理像素）
    bool m_translateMode = false; // 当前 OCR 请求是否为"识别并翻译"（【译】按钮）
    QString m_lastText; // 最近一次识别文字（供【译】触发翻译）
    QWidget *m_parent = nullptr; // 当前操作父窗口（供 QMessageBox 使用，避免被置顶窗口遮挡）
};

} // namespace snipaste
