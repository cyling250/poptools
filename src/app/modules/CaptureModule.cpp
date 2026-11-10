#include "CaptureModule.h"

#include <QCoreApplication>
#include <QPixmap>
#include <QRect>
#include <QTimer>

#include "../AppServices.h"
#include "core/assistant/ToolRegistry.h"
#include "core/hotkey/HotKeyNames.h"
#include "core/settings/Settings.h"
#include "ui/capture/CaptureWindow.h"

namespace poptools {

void CaptureModule::init(AppServices &services)
{
    m_services = &services;

    // 注入共享 OCR 引擎（全局唯一子进程；null 属装配缺陷，CaptureWindow
    // 内 Q_ASSERT 拦截——T-036 批次 D 已删除内部自建兜底，防拉起第二个引擎）
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

    // Goody 工具贡献：模型可发起截图（UI 操作延迟到事件循环，与热键同纪律）
    if (services.toolRegistry) {
        ToolDef d;
        d.name = QStringLiteral("app.capture");
        d.readOnly = true;
        d.description = QStringLiteral("启动 PopTools 截图（进入全屏选区模式）。");
        d.parameters = QJsonObject{{"type", QStringLiteral("object")},
                                   {"properties", QJsonObject{}}};
        d.fn = [this](const QJsonObject &, const std::function<void(QJsonObject)> &done) {
            QTimer::singleShot(0, QCoreApplication::instance(), [this] {
                if (!m_window->isVisible())
                    m_window->startCapture();
            });
            done(QJsonObject{{"ok", true}, {"note", QStringLiteral("已启动截图选区模式")}});
        };
        services.toolRegistry->registerTool(d);
    }
}

void CaptureModule::shutdown()
{
    // 反装配：撤销发布的能力槽。模块对象析构早于 AppServices（组合根成员
    // 逆序析构），不清空则槽位在析构窗口内悬垂
    if (m_services) {
        m_services->captureActive = nullptr;
        m_services->captureIntercept = nullptr;
    }
}

} // namespace poptools
