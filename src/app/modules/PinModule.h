#pragma once

#include "../IFeatureModule.h"

#include <QObject>

#include <QPixmap>
#include <QVector>

#include "core/ocr/RapidOcrService.h"

namespace poptools {

class AppServices;
class Translator;

// ============================================================
// 贴图功能模块：F3 全局热键把剪贴板最新图片张贴到屏幕顶层。
//
// 消费 CaptureModule 发布的能力：
//   * captureIntercept —— 截图进行中先复制当前截图并关闭（作为贴图来源），
//     同时取其选区作为张贴位置；
//   * lastCaptureRect —— 非截图态下的张贴位置回退。
// 贴图窗口不常驻：每次 F3 新建 PinWindow，关闭即销毁（WA_DeleteOnClose）。
//
// 贴图右键「识别文字 / 翻译」：经共享 OCR 服务识别贴图内容，
// 文本/译文进剪贴板并以托盘气泡反馈（不弹交互窗）。
// ============================================================
class PinModule : public QObject, public IFeatureModule
{
    Q_OBJECT
public:
    explicit PinModule(QObject *parent = nullptr);
    QString id() const override { return QStringLiteral("pin"); }
    void init(AppServices &services) override;

private slots:
    void pinFromClipboard();
    void onPinOcrRequested(const QPixmap &img);
    void onPinTranslateRequested(const QPixmap &img);
    void onPinOcrFinished(bool ok, const QVector<OcrBox> &boxes, const QString &error);
    void onTranslateFinished(bool ok, const QString &result, const QString &error);

private:
    void startPinOcr(const QPixmap &img);

    AppServices *m_services = nullptr;
    Translator *m_translator = nullptr;   // 贴图翻译（本模块独享消费者）
    bool m_ocrThenTranslate = false;      // 当前贴图 OCR 请求是否续翻
};

} // namespace poptools
