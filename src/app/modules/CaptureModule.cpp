#include "CaptureModule.h"

#include <QPixmap>
#include <QRect>

#include "../AppServices.h"
#include "core/hotkey/HotKeyNames.h"
#include "core/settings/Settings.h"
#include "ui/capture/CaptureWindow.h"

namespace poptools {

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

    // 先注册截图全局热键（vk 可在设置中修改），注册结果决定托盘文案——
    // 热键被其他程序占用时（如其他截图工具），让用户在菜单里直接看出来
    const quint32 captureVk = Settings::instance().captureHotKeyVk();
    const bool hotkeyOk = services.registerHotKey(captureVk, 0, [this] {
        if (!m_window->isVisible())
            m_window->startCapture();
    });

    // 托盘入口
    services.addTrayAction(hotkeyOk
        ? QStringLiteral("截图 (%1)").arg(hotKeyDisplayName(captureVk))
        : QStringLiteral("截图（%1 已被占用）").arg(hotKeyDisplayName(captureVk)),
        [this] {
            if (!m_window->isVisible())
                m_window->startCapture();
        });
}

} // namespace poptools
