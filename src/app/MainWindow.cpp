#include "MainWindow.h"

#include <QApplication>
#include <QMessageBox>

#include "AppServices.h"
#include "core/ocr/PaddleOcrService.h"
#include "modules/CaptureModule.h"
#include "modules/PinModule.h"
#include "modules/SelectionTranslateModule.h"
#include "ui/settings/SettingsDialog.h"

namespace snipaste {

MainWindow::MainWindow(QObject *parent)
    : QObject(parent)
{
    // 托盘（使用资源图标：Material Design 相机图标，Apache 2.0 许可）
    m_tray = new QSystemTrayIcon(QIcon(":/app_icon.svg"), this);
    m_tray->setToolTip("Snipaste\n按 F1 截图");

    m_menu = new QMenu();
    m_menu->setObjectName("TrayMenu");
    m_menu->setTitle("Snipaste");

    // ---- 共享服务（组合根持有所有权，注入各功能模块） ----
    m_ocr = new PaddleOcrService(this);
    m_services = new AppServices(this);
    m_services->hotkey = &m_hotKey;
    m_services->ocr = m_ocr;
    m_services->trayMenu = m_menu;

    // ---- 注册功能模块（注册顺序 = 托盘菜单中功能项顺序；拔插功能改此处） ----
    m_registry.add(std::make_unique<CaptureModule>());
    m_registry.add(std::make_unique<PinModule>());
    m_registry.add(std::make_unique<SelectionTranslateModule>());
    m_registry.initAll(*m_services);

    // ---- 外壳自有菜单项（排在模块贡献之后） ----
    m_menu->addSeparator();
    m_menu->addAction("设置...", this, &MainWindow::onShowSettings);
    m_menu->addSeparator();
    m_menu->addAction("关于", [] {
        QMessageBox::about(nullptr, "Snipaste",
            "Snipaste v0.2.0\n\n"
            "全场景截图软件：区域截图 + OCR 识别 + 翻译\n\n"
            "快捷键：F1 截图\n"
            "OCR 引擎：PaddleOCR（本地离线）\n"
            "翻译：OpenAI 兼容大模型 API（可配置）");
    });
    m_menu->addSeparator();
    m_menu->addAction("退出", this, &QApplication::quit);

    m_tray->setContextMenu(m_menu);
    m_tray->show();
}

void MainWindow::onShowSettings()
{
    SettingsDialog dlg(nullptr);
    dlg.exec();
    // 设置保存后刷新运行时组件：Translator 每次 translate() 读取最新 Settings，
    // 无需额外操作；若有缓存配置的组件，在此处刷新。
}

} // namespace snipaste
