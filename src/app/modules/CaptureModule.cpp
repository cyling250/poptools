#include "CaptureModule.h"

#include <QCoreApplication>
#include <QPixmap>
#include <QRect>
#include <QTimer>

#include "../AppServices.h"
#include "core/hotkey/HotKeyNames.h"
#include "core/monitor/MainThreadProbe.h"
#include "core/settings/Settings.h"
#include "ui/capture/CaptureWindow.h"

namespace poptools {

// 装配截图链：建 CaptureWindow → 发布 captureActive/Intercept 槽位 →
// 注册 F1 → 托盘入口（文案随热键注册结果）→ 贡献 app.capture 工具
void CaptureModule::init(AppServices &services)
{
    m_services = &services;

    // 注入共享 OCR 引擎（全局唯一子进程；null 属装配缺陷，CaptureWindow
    // 内 Q_ASSERT 拦截——T-036 批次 D 已删除内部自建兜底，防拉起第二个引擎）
    m_window = new CaptureWindow(services.ocr);

    // 捕获完成 → 记录选区位置（F3 贴图定位用）。
    // 生命周期论证：connect 的接收者上下文是 m_window 而 lambda 捕获模块
    // this（CaptureModule 非 QObject，无法作上下文）——安全性依赖"模块只在
    // 应用退出时析构、窗口与模块同寿命且退出后无信号"；若日后模块可动态
    // 拔插，须改为 shutdown 时 disconnect
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

    // 先注册截图全局热键（vk 可在设置中修改）。注册失败由气泡告知
    //（takeFailedHotKeyNames，T-065 起托盘不再重复文案）
    const quint32 captureVk = Settings::instance().captureHotKeyVk();
    services.registerHotKey(captureVk, 0, [this] {
        // T-062 修复: 热键回调在 GlobalHotKey 的原生事件过滤器栈中直发，
        // 直接建窗/激活违反 AppServices 契约（"GUI 操作请自行延迟到事件
        // 循环"）——对齐 PinModule/划词翻译的
        // singleShot 先例
        // T-069b F1 卡顿取证：热键回调与启动两段打点（探针 mark 全量落盘，
        // 与 cap_begin/cap_grabbed/cap_shown 分段按 QPC 对时还原完整链路）
        if (auto *p = MainThreadProbe::instance())
            p->mark("f1_hotkey");
        QTimer::singleShot(0, QCoreApplication::instance(), [this] {
            if (auto *p = MainThreadProbe::instance())
                p->mark("f1_start");
            if (!m_window->isVisible())
                m_window->startCapture();
        });
    });

    // T-069b 托盘菜单精简：「截图」托盘入口撤除（F1 热键即入口），菜单只留
    // 设置/关于/退出（需求单 requirements/20261119-tray-menu-f1-probe.md）
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
