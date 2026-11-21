#include "MainWindow.h"

#include "ExternalSyncWatcher.h"

#include <QApplication>
#include <QDesktopServices>
#include <QMessageBox>
#include <QScreen>
#include <QTimer>
#include <QUrl>
#include <QStandardPaths>

#include "AppServices.h"
#include "core/monitor/MainThreadProbe.h"
#include "core/ocr/RapidOcrService.h"
#include "core/settings/Settings.h"
#include "core/update/UpdateChecker.h"
#include "modules/ActivityMonitorModule.h"
#include "modules/BoxModule.h"
#include "modules/CalendarModule.h"
#include "modules/CaptureModule.h"
#include "modules/GoodyModule.h"
#include "modules/PinModule.h"
#include "modules/SelectionTranslateModule.h"
#include "ui/settings/SettingsDialog.h"
#include "ui/shared/DesktopSurfaceHost.h"

namespace poptools {

// 装配顺序即生命周期契约：共享服务 → 模块 initAll → 外壳自有菜单项；
// 成员析构逆序保证模块先于服务消亡
MainWindow::MainWindow(QObject *parent)
    : QObject(parent)
{
    // T-068 主线程输入探针：必须在任何模块装钩子之前建好（探针的心跳与
    // 钩子打点都在主线程，见 MainThreadProbe 头注释）；根因定案后随探针
    // 一并移除
    MainThreadProbe::instance();
    // 托盘（使用资源图标：Material Design 相机图标，Apache 2.0 许可）
    m_tray = new QSystemTrayIcon(QIcon(":/app_icon.svg"), this);
    m_tray->setToolTip("PopTools\n按 F1 截图");

    // QMenu 的父对象只能是 QWidget（本类是纯 QObject），所有权在析构中显式处理；
    // QSystemTrayIcon::setContextMenu 不接管所有权
    m_menu = new QMenu();
    m_menu->setObjectName("TrayMenu");
    m_menu->setTitle("PopTools");

    // ---- 共享服务（组合根持有所有权，注入各功能模块） ----
    m_ocr = new RapidOcrService(this);
    m_services = new AppServices(this);
    m_services->hotkey = &m_hotKey;
    m_services->ocr = m_ocr;
    m_services->trayMenu = m_menu;
    // 桌面表面：注入通道 + 共享画布 + 面板事件路由（日历/收纳盒等贴壁纸
    // 面板共用；本体只有隐藏 1x1 窗，重活在 startDuty，由模块延迟触发）
    m_desktopSurface = new DesktopSurfaceHost();
    m_services->desktopSurface = m_desktopSurface;
    // 托盘气泡通知（模块的非交互结果反馈，如贴图 OCR 完成/失败）
    m_services->notify = [this](const QString &title, const QString &body) {
        m_tray->showMessage(title, body, QSystemTrayIcon::Information);
    };
    // 日历模块托盘「日历设置」入口：打开应用设置对话框并定位桌面日历页
    m_services->openCalendarSettings = [this] { onShowSettings(true); };

    // ---- 注册功能模块（注册顺序 = 托盘菜单中功能项顺序；拔插功能改此处） ----
    m_registry.add(std::make_unique<CaptureModule>());
    m_registry.add(std::make_unique<PinModule>());
    m_registry.add(std::make_unique<SelectionTranslateModule>());
    m_registry.add(std::make_unique<ActivityMonitorModule>());
    m_registry.add(std::make_unique<GoodyModule>());
    m_registry.add(std::make_unique<CalendarModule>());
    m_registry.add(std::make_unique<BoxModule>());
    m_registry.initAll(*m_services);

    // ---- 跨进程生效协议（T-074 M2.5）：poptools_mcp_server（dsh 拉起的独立
    // 进程）代表 agent 改写数据文件后 touch dirty 标记，此处统一收口——
    // settings → sync + 广播；calendar → 宿主重载；box → 广播（重应用）----
    const QString dataRoot =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    auto *syncWatcher = new ExternalSyncWatcher(dataRoot, this);
    connect(syncWatcher, &ExternalSyncWatcher::settingsDirty, this, [this] {
        Settings::instance().syncExternal();
        m_services->notifySettingsChanged();
    });
    connect(syncWatcher, &ExternalSyncWatcher::calendarDirty, this, [this] {
        if (m_services->calendarApplySettings)
            m_services->calendarApplySettings();
    });
    connect(syncWatcher, &ExternalSyncWatcher::boxDirty, this, [this] {
        Settings::instance().syncExternal();
        m_services->notifySettingsChanged();
    });

    // 能力槽完整性自检：拔掉 CaptureModule 时贴图会静默退化为"剪贴板 + 鼠标位置"，
    // 显式告警避免静默降级（模块间依赖在注册顺序上隐式成立，此处是唯一护栏）
    if (!m_services->captureIntercept)
        qWarning("CaptureModule 未提供 captureIntercept 能力槽，贴图定位退化为剪贴板模式");

    // ---- 外壳自有菜单项（排在模块贡献之后） ----
    m_menu->addSeparator();
    m_menu->addAction("设置...", this, &MainWindow::onShowSettings);
    m_menu->addSeparator();
    m_menu->addAction("关于", [this] {
        QMessageBox box(nullptr);
        box.setWindowTitle(QStringLiteral("关于 PopTools"));
        // 构建时间用于辨别安装的是哪一版包（多轮修复期间包名相同易混淆）
        box.setText(QStringLiteral("PopTools v" POPTOOLS_VERSION "\n\n"
                                   "全场景截图软件：区域截图 + OCR 识别 + 翻译\n\n"
                                   "快捷键：F1 截图\n"
                                   "OCR 引擎：RapidOCR（本地离线）\n"
                                   "翻译：OpenAI 兼容大模型 API（可配置）\n\n"
                                   "构建时间：%1 %2")
                        .arg(QLatin1String(__DATE__), QLatin1String(__TIME__)));
        // 标准按钮 + setButtonText（绕开 QAbstractButton* 判定，exec 返回值判定）
        box.setStandardButtons(QMessageBox::Ok | QMessageBox::Yes);
        box.setButtonText(QMessageBox::Yes, QStringLiteral("检查更新"));
        if (box.exec() == QMessageBox::Yes)
            doUpdateCheck(true);
    });
    m_menu->addSeparator();
    m_menu->addAction("退出", this, &QApplication::quit);

    m_tray->setContextMenu(m_menu);
    m_tray->show();

    // T-069b F1 卡顿取证：托盘菜单弹出/关闭打链路标记（探针 mark 全量落盘，
    // 与 F1 链路分段标记按 QPC 对时——定位"菜单拉起时 F1 卡顿"的竞争关系）
    connect(m_menu, &QMenu::aboutToShow, this, [] {
        if (auto *p = MainThreadProbe::instance())
            p->mark("tray_menu_show");
    });
    connect(m_menu, &QMenu::aboutToHide, this, [] {
        if (auto *p = MainThreadProbe::instance())
            p->mark("tray_menu_hide");
    });

    // 热键注册失败的统一告知（模块 init 时记录；气泡须在托盘可见后弹出）。
    // 失败名已带具体原因（"F1：已被其他程序占用"/Win32 错误码），此处不再复述。
    // 聚合为一条气泡：Windows 托盘气泡同一时刻只显示一条，逐条弹后发覆盖
    // 先发，失败 2 个以上时用户只能看到最后一条（T-062）
    const QStringList failedHotKeys = m_services->takeFailedHotKeyNames();
    if (!failedHotKeys.isEmpty()) {
        m_tray->showMessage(QStringLiteral("热键注册失败"),
            QStringLiteral("%1\n对应快捷键暂不可用，仍可通过托盘菜单使用相应功能。")
                .arg(failedHotKeys.join(QLatin1Char('\n'))),
            QSystemTrayIcon::Warning);
    }

    // 启动更新自查：延迟 5s（避开启动路径，红线 6），静默模式——仅发现新版时
    // 弹提示，无更新/失败不打扰；用户"跳过此版本"后同版本不再提示
    QTimer::singleShot(5000, this, [this] { doUpdateCheck(false); });
}

void MainWindow::doUpdateCheck(bool interactive)
{
    auto *checker = new UpdateChecker(this);
    connect(checker, &UpdateChecker::updateAvailable, this,
            [this, interactive](const QString &version, const QString &url) {
                // 启动静默模式：用户跳过过的版本不再提示（手动检查不受限）
                if (!interactive && version == Settings::instance().updateIgnoredVersion())
                    return;
                QMessageBox box(nullptr);
                box.setWindowTitle(QStringLiteral("发现新版本"));
                box.setText(QStringLiteral("PopTools %1 已发布（当前版本 v%2）。\n\n"
                                           "是否前往下载页获取安装包？")
                                .arg(version, QLatin1String(POPTOOLS_VERSION)));
                // 标准按钮 + setButtonText（exec 返回值判定，绕开 QAbstractButton*）
                box.setStandardButtons(QMessageBox::Yes | QMessageBox::No
                                       | QMessageBox::Cancel);
                box.setButtonText(QMessageBox::Yes, QStringLiteral("前往下载"));
                box.setButtonText(QMessageBox::No, QStringLiteral("跳过此版本"));
                box.setButtonText(QMessageBox::Cancel, QStringLiteral("关闭"));
                const int pick = box.exec();
                if (pick == QMessageBox::Yes) {
                    QDesktopServices::openUrl(QUrl(url));
                } else if (pick == QMessageBox::No && interactive) {
                    Settings::instance().setUpdateIgnoredVersion(version);
                }
            });
    connect(checker, &UpdateChecker::upToDate, this, [interactive] {
        if (interactive)
            QMessageBox::information(nullptr, QStringLiteral("检查更新"),
                                     QStringLiteral("当前已是最新版本。"));
    });
    connect(checker, &UpdateChecker::checkFailed, this, [interactive](const QString &reason) {
        if (interactive)
            QMessageBox::warning(nullptr, QStringLiteral("检查更新失败"), reason);
    });
    checker->checkForUpdates(interactive);
}

MainWindow::~MainWindow()
{
    // 退出前反装配：模块在其 shutdown 中撤销发布到 AppServices 的能力槽，
    // 避免槽位/热键回调在成员析构窗口内访问悬垂的模块 this
    m_registry.shutdownAll();
    // T-062 修复: m_desktopSurface 无 parent、无他人 delete，原先析构体永不
    // 执行 → 退出路径注入解除（stopDuty→StopDock）断链，explorer 内注入 DLL
    // 与共享画布跨进程残留。此处确定性析构（quit 流程：托盘「退出」→
    // app.exec() 返回 → 本析构体，此时 QApplication 仍在，删 QWidget 安全）。
    // 顺序关键：先 shutdownAll 让模块 unregisterPanel，再停值守——恰为
    // stopDuty/析构体内部顺序；无双重释放（所有权唯一在本类）
    delete m_desktopSurface;
    m_desktopSurface = nullptr;
    if (m_services)
        m_services->desktopSurface = nullptr; // 消除悬垂槽位指针
    delete m_menu; // 托盘菜单无 QWidget 父对象，显式释放（action 随之销毁）
    m_menu = nullptr;
}

// 模态打开设置对话框；openCalendarPage 为 true 时定位桌面日历页
//（Goody 日历托盘入口复用）。2026-11-19 Fluent 改版（M1）：设置即时生效——
// 对话框内每次变更已自行写 Settings 并经 notifySettingsChanged 广播，
// 这里不再有 Accepted 后统一广播（关闭/Esc 路径同样已生效，防抖冲刷由
// SettingsDialog::done 负责）
void MainWindow::onShowSettings(bool openCalendarPage)
{
    SettingsDialog dlg(m_services);
    if (openCalendarPage)
        dlg.openCalendarPage();
    // T-062 修复: 无 parent 模态对话框落屏由系统默认决定，多屏不可控
    //（P-059 家族：无主 QDialog 曾落副屏）——按主屏可用区居中（对齐
    // GoodyWindow 几何先例）。adjustSize 先定尺寸（与 show 时未显式 resize
    // 的 adjustedSize 同源），再按该尺寸居中
    dlg.adjustSize();
    const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
    dlg.move(avail.center() - dlg.rect().center());
    dlg.exec();
}

} // namespace poptools
#include "app/ExternalSyncWatcher.h"
