#include "PinModule.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QGuiApplication>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QTimer>
#include <algorithm>

#include "../AppServices.h"
#include "core/monitor/OwnWriteGuard.h" // T-047-CLIP
#include "core/assistant/ToolRegistry.h"
#include "core/ocr/RapidOcrService.h"
#include "core/settings/Settings.h"
#include "core/translation/Translator.h"
#include "ui/pin/PinWindow.h"

namespace poptools {

// 仅作 QObject 接线中枢；无常驻运行期对象（贴图窗口按次建、随关随销）
PinModule::PinModule(QObject *parent)
    : QObject(parent)
{
}

void PinModule::init(AppServices &services)
{
    m_services = &services;

    // 贴图 OCR/翻译的专属翻译服务（单服务单消费者；设置直读，与划词卡片实例互不影响）
    m_translator = new Translator(this);
    connect(m_translator, &Translator::translateFinished, this,
            &PinModule::onTranslateFinished);

    // F3：贴剪贴板最新图片。热键在原生事件过滤器中触发：UI 操作（窗口创建/激活）
    // 延迟到事件循环，避免时序问题。
    // vk 硬编码 VK_F3 为有意固定（贴图热键未开放设置项；其余功能热键均可配）
    services.registerHotKey(0x72 /* VK_F3 */, 0, [this] {
        QTimer::singleShot(0, QCoreApplication::instance(),
                           [this] { pinFromClipboard(); });
    });

    // Goody 工具贡献：模型可把剪贴板图片贴到屏幕
    if (services.toolRegistry) {
        ToolDef d;
        d.name = QStringLiteral("app.pin_clipboard");
        d.readOnly = true;
        d.description = QStringLiteral(
            "把剪贴板中最新的图片贴为 PopTools 屏幕贴图（置顶悬浮）。"
            "剪贴板没有图片时返回失败。");
        d.parameters = emptyObjectSchema();
        d.fn = [this](const QJsonObject &, const std::function<void(QJsonObject)> &done) {
            QTimer::singleShot(0, QCoreApplication::instance(), [this, done] {
                if (QGuiApplication::clipboard()->pixmap().isNull()) {
                    done(QJsonObject{{"ok", false},
                                     {"error", QStringLiteral("剪贴板中没有图片")}});
                    return;
                }
                pinFromClipboard();
                done(QJsonObject{{"ok", true}, {"note", QStringLiteral("已张贴剪贴板图片")}});
            });
        };
        services.toolRegistry->registerTool(d);
    }
}

void PinModule::pinFromClipboard()
{
    // 截图模式：先复制当前截图（含标注）并关闭截图模式（作为"剪贴板最新内容"），
    // 同时取本次选区位置作为张贴位置
    QRect captureSel;
    if (m_services->captureIntercept)
        captureSel = m_services->captureIntercept();

    // 从剪贴板取最新图片张贴；剪贴板为文字时 pixmap 为空 → 无响应
    const QPixmap img = QGuiApplication::clipboard()->pixmap();
    if (img.isNull())
        return;

    // 张贴位置：本次截图位置 > 最近一次确认截图位置 > 鼠标当前位置（作为左上角点）
    QPoint pos;
    if (!captureSel.isEmpty())
        pos = captureSel.topLeft();
    else if (!m_services->lastCaptureRect.isEmpty())
        pos = m_services->lastCaptureRect.topLeft();
    else
        pos = QCursor::pos();

    // 跨屏混合 DPI（R3 #2）：仅当本次 F3 拦截了进行中的截图时，剪贴板内容
    // 才可确定来自该选区所在屏——以选区左上角所在屏 dpr 作为内容源屏 dpr 传入；
    // 其余来源（纯剪贴板图）源屏未知，传 0 维持旧行为（PinWindow 按张贴屏反推）
    qreal sourceDpr = 0.0;
    if (!captureSel.isEmpty())
    {
        if (const QScreen *scr = QGuiApplication::screenAt(captureSel.topLeft()))
            sourceDpr = scr->devicePixelRatio();
    }

    // 多张贴图并存：每次 F3 创建新实例，互不影响；关闭时（WA_DeleteOnClose）自动销毁
    auto *pin = new PinWindow();
    // 右键「识别文字 / 翻译」：context = pin，窗口销毁自动断连
    connect(pin, &PinWindow::ocrRequested, this, &PinModule::onPinOcrRequested);
    connect(pin, &PinWindow::translateRequested, this, &PinModule::onPinTranslateRequested);
    pin->pin(img, pos, sourceDpr);
}

// ---- 贴图 OCR / 翻译（结果走托盘气泡 + 剪贴板，不弹交互窗） ----

// 贴图右键「识别文字」入口：纯 OCR
void PinModule::onPinOcrRequested(const QPixmap &img)
{
    m_ocrThenTranslate = false;
    startPinOcr(img);
}

// 「翻译」入口：OCR 后按记忆目标语言续翻（m_ocrThenTranslate 链路开关）
void PinModule::onPinTranslateRequested(const QPixmap &img)
{
    m_ocrThenTranslate = true;
    startPinOcr(img);
}

void PinModule::startPinOcr(const QPixmap &img)
{
    RapidOcrService *ocr = m_services->ocr;
    if (!ocr || !ocr->available()) {
        if (m_services->notify)
            m_services->notify(QStringLiteral("贴图 OCR"), QStringLiteral("OCR 引擎不可用"));
        return;
    }
    if (ocr->busy()) {
        if (m_services->notify)
            m_services->notify(QStringLiteral("贴图 OCR"),
                               QStringLiteral("OCR 正忙（截图识别进行中），请稍后再试"));
        return;
    }
    // 单次请求连接（SingleShotConnection：回包后自动断开，不与截图模块的常驻
    // 连接互相干扰；结果归属防护另见 OcrController::onBoxes 的 m_ocrPending）
    connect(ocr, &RapidOcrService::ocrBoxesFinished, this, &PinModule::onPinOcrFinished,
            Qt::SingleShotConnection);
    ocr->recognizeBoxes(img.toImage());
    if (m_services->notify)
        m_services->notify(QStringLiteral("贴图 OCR"), QStringLiteral("正在识别…"));
}

void PinModule::onPinOcrFinished(bool ok, const QVector<OcrBox> &boxes, const QString &error)
{
    if (!ok) {
        if (m_services->notify)
            m_services->notify(QStringLiteral("贴图 OCR"),
                               error.isEmpty() ? QStringLiteral("未识别到文字") : error);
        return;
    }
    // 行序重排（阅读顺序：先上后下、同行先左后右）后拼接——半行高容差
    // 同行判定抽到 core/ocr 共享（截图翻译同款），见 sortOcrBoxesReadingOrder
    QVector<OcrBox> sorted = boxes;
    sortOcrBoxesReadingOrder(sorted);
    QString text;
    for (const OcrBox &box : sorted) {
        const QString t = box.text.trimmed();
        if (t.isEmpty())
            continue;
        if (!text.isEmpty())
            text += QLatin1Char('\n');
        text += t;
    }
    if (text.isEmpty()) {
        if (m_services->notify)
            m_services->notify(QStringLiteral("贴图 OCR"), QStringLiteral("未识别到文字"));
        return;
    }

    // 仅识别：文本进剪贴板；翻译：以记忆目标语言续翻，译文进剪贴板
    if (!m_ocrThenTranslate) {
        QGuiApplication::clipboard()->setText(text);
        OwnWriteGuard::noteSelfWrite(); // T-047-CLIP：自写排除
        if (m_services->notify)
            m_services->notify(QStringLiteral("贴图 OCR"), QStringLiteral("文字已复制到剪贴板"));
        return;
    }
    m_translator->translate(text, QStringLiteral("auto"),
                            Settings::instance().translateTargetLang(), false);
}

void PinModule::onTranslateFinished(bool ok, const QString &result, const QString &error)
{
    if (ok) {
        QGuiApplication::clipboard()->setText(result);
        OwnWriteGuard::noteSelfWrite(); // T-047-CLIP：自写排除
        if (m_services->notify)
            m_services->notify(QStringLiteral("贴图翻译"), QStringLiteral("译文已复制到剪贴板"));
    } else {
        if (m_services->notify)
            m_services->notify(QStringLiteral("贴图翻译"),
                               QStringLiteral("翻译失败：%1").arg(error));
    }
}

} // namespace poptools
