#include "CaptureModule.h"

#include <QPixmap>
#include <QRect>

#include "../AppServices.h"
#include "core/settings/Settings.h"
#include "ui/capture/CaptureWindow.h"

namespace snipaste {

void CaptureModule::init(AppServices &services)
{
    m_services = &services;

    // 注入共享 OCR 引擎（全局唯一子进程；null 时 CaptureWindow 内部自建兜底）
    m_window = new CaptureWindow(services.ocr);

    // 捕获完成 → 记录选区位置（F3 贴图定位用）
    QObject::connect(m_window, &CaptureWindow::captured, m_window,
                     [this](const QPixmap &, const QRect &selection) {
                         if (!selection.isEmpty())
                             m_services->lastCaptureRect = selection;
                     });

    // 能力槽：截图态查询与"复制并关闭"拦截（Pin / 划词翻译消费）
    services.captureActive = [this] { return m_window->isVisible(); };
    services.captureIntercept = [this] {
        return m_window->isVisible() ? m_window->copyToClipboardAndClose() : QRect{};
    };

    // 托盘入口 + F1 全局热键（vk 可在设置中修改）
    services.addTrayAction(QStringLiteral("截图 (F1)"), [this] {
        if (!m_window->isVisible())
            m_window->startCapture();
    });
    services.registerHotKey(Settings::instance().captureHotKeyVk(), 0, [this] {
        if (!m_window->isVisible())
            m_window->startCapture();
    });
}

} // namespace snipaste
