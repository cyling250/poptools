#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

#include "core/ocr/PaddleOcrService.h"
#include "CaptureContext.h"

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace snipaste {

// ============================================================
// OCR 功能对宿主（CaptureWindow）的最小依赖：文字栏面板与扫描动效回调。
// 独立于 CaptureContext（画布/选区共享态），避免功能回调污染通用上下文；
// 由 CaptureWindow 在构造 OcrController 时注入。
// ============================================================
struct OcrHostCallbacks
{
    // OCR 扫描动效开关（true=开始，false=结束）
    std::function<void(bool)> ocrBusy;
    // 文字栏翻译忙状态（OCR/翻译进行中禁用翻译按钮）
    std::function<void(bool)> setPanelBusy;
    // 面板模式：显示文字栏（参数为 OCR 识别文字，可为空串先占位）
    std::function<void(const QString &)> showPanel;
    // 【译】模式：识别完成后自动翻译
    std::function<void(const QString &)> translateRequested;
};

// ============================================================
// OCR 控制器：把选区内图像（物理分辨率）交给 PaddleOCR，
// 结果文本框映射回选区局部坐标并作为文字标注添加到画板。
// 继承 QObject 仅为了以 this 作信号连接的 context 对象
// （OCR 服务为共享实例，寿命长于本控制器，必须随控制器析构自动断连）。
// ============================================================
class OcrController : public QObject
{
public:
    // ocr：共享 OCR 引擎（应用级唯一，非拥有，寿命须覆盖本控制器）
    OcrController(CaptureContext *ctx, PaddleOcrService *ocr, OcrHostCallbacks host);
    ~OcrController() override;

    bool busy() const { return m_ocr->busy(); }
    bool available() const { return m_ocr->available(); }

    void request(QWidget *parent); // 检查状态 + 裁剪选区图像 + 启动识别
    void requestTranslate(QWidget *parent); // 识别并翻译：结果显示到文字栏面板（原始+翻译）

private:
    bool prepareAndRecognize(QWidget *parent); // 公共准备（检查状态 + 裁剪 + 启动识别）；不可用返回 false
    void onBoxes(bool ok, const QVector<OcrBox> &boxes, const QString &error);
    void onBoxesTranslate(bool ok, const QVector<OcrBox> &boxes, const QString &error);
    static QFont fitFontHeight(const QRectF &local, const QString &text); // 覆盖文字字号与原文字墨迹高度匹配（实测逼近）

    CaptureContext *m_ctx;
    PaddleOcrService *m_ocr; // 共享 OCR 引擎（非拥有）
    OcrHostCallbacks m_host; // 宿主回调（面板/动效，非拥有）
    QImage m_ocrImg; // 传给 OCR 的选区裁剪图（物理像素）
    bool m_translateMode = false; // 当前 OCR 请求是否为"识别并翻译"（【译】按钮）
    QString m_lastText; // 最近一次识别文字（供【译】触发翻译）
    QWidget *m_parent = nullptr; // 当前操作父窗口（供 QMessageBox 使用，避免被置顶窗口遮挡）
};

} // namespace snipaste
