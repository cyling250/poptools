#include "MainWindow.h"

#include <QApplication>
#include <QMessageBox>

#include "AppServices.h"
#include "core/ocr/PaddleOcrService.h"
#include "modules/CaptureModule.h"
#include "modules/PinModule.h"
#include "modules/SelectionTranslateModule.h"
#include "ui/settings/SettingsDialog.h"

namespace poptools {

MainWindow::MainWindow(QObject *parent)
    : QObject(parent)
{
    // 托盘（使用资源图标：Material Design 相机图标，Apache 2.0 许可）
    m_tray = new QSystemTrayIcon(QIcon(":/app_icon.svg"), this);
    m_tray->setToolTip("PopTools\n按 F1 截图");

    // QMenu 的父对象只能是 QWidget（本类是纯 QObject），所有权在析构中显式处理；
    // QSystemTrayIcon::setContextMenu 不接管所有权
    m_menu = new QMenu();
    m_menu->setObjectName("TrayMenu");
    m_menu->setTitle("PopTools");

    // ---- 共享服务（组合根持有所有权，注入各功能模块） ----
    m_ocr = new PaddleOcrService(this);
    m_services = new AppServices(this);
    m_services->hotkey = &m_hotKey;
    m_services->ocr = m_ocr;
    m_services->trayMenu = m_menu;
    // 托盘气泡通知（模块的非交互结果反馈，如贴图 OCR 完成/失败）
    m_services->notify = [this](const QString &title, const QString &body) {
        m_tray->showMessage(title, body, QSystemTrayIcon::Information);
    };

    // ---- 注册功能模块（注册顺序 = 托盘菜单中功能项顺序；拔插功能改此处） ----
    m_registry.add(std::make_unique<CaptureModule>());
    m_registry.add(std::make_unique<PinModule>());
    m_registry.add(std::make_unique<SelectionTranslateModule>());
    m_registry.initAll(*m_services);

    // 能力槽完整性自检：拔掉 CaptureModule 时贴图会静默退化为"剪贴板 + 鼠标位置"，
    // 显式告警避免静默降级（模块间依赖在注册顺序上隐式成立，此处是唯一护栏）
    if (!m_services->captureIntercept)
        qWarning("CaptureModule 未提供 captureIntercept 能力槽，贴图定位退化为剪贴板模式");

    // ---- 外壳自有菜单项（排在模块贡献之后） ----
    m_menu->addSeparator();
    m_menu->addAction("设置...", this, &MainWindow::onShowSettings);
    m_menu->addSeparator();
    m_menu->addAction("关于", [] {
        // 构建时间用于辨别安装的是哪一版包（多轮修复期间包名相同易混淆）
        QMessageBox::about(nullptr, "PopTools",
            QStringLiteral("PopTools v0.3.2\n\n"
                           "全场景截图软件：区域截图 + OCR 识别 + 翻译\n\n"
                           "快捷键：F1 截图\n"
                           "OCR 引擎：PaddleOCR（本地离线）\n"
                           "翻译：OpenAI 兼容大模型 API（可配置）\n\n"
                           "构建时间：%1 %2")
                .arg(QLatin1String(__DATE__), QLatin1String(__TIME__)));
    });
    m_menu->addSeparator();
    m_menu->addAction("退出", this, &QApplication::quit);

    m_tray->setContextMenu(m_menu);
    m_tray->show();

    // 热键注册失败的统一告知（模块 init 时记录；气泡须在托盘可见后弹出）。
    // 失败名已带具体原因（"F1：已被其他程序占用"/Win32 错误码），此处不再复述
    const QStringList failedHotKeys = m_services->takeFailedHotKeyNames();
    for (const QString &name : failedHotKeys) {
        m_tray->showMessage(QStringLiteral("热键注册失败"),
            QStringLiteral("%1，对应快捷键暂不可用，仍可通过托盘菜单使用相应功能。")
                .arg(name),
            QSystemTrayIcon::Warning);
    }
}

MainWindow::~MainWindow()
{
    // 退出前反装配：模块在其 shutdown 中撤销发布到 AppServices 的能力槽，
    // 避免槽位/热键回调在成员析构窗口内访问悬垂的模块 this
    m_registry.shutdownAll();
    delete m_menu; // 托盘菜单无 QWidget 父对象，显式释放（action 随之销毁）
    m_menu = nullptr;
}

void MainWindow::onShowSettings()
{
    SettingsDialog dlg(nullptr);
    dlg.exec();
    // 设置保存后刷新运行时组件：Translator 每次 translate() 读取最新 Settings，
    // 无需额外操作；若有缓存配置的组件，在此处刷新。
}

} // namespace poptools
