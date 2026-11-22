#pragma once

#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>
#include <QTimer>

#include "FeatureRegistry.h"
#include "core/hotkey/GlobalHotKey.h"

namespace poptools {

class AppServices;
class DesktopSurfaceHost;
class CaptureHistoryJob;
class RapidOcrService;

// ============================================================
// 应用外壳（组合根）：系统托盘 + 共享服务装配 + 功能模块注册。
//
// 各功能（截图 / 贴图 / 划词翻译）以 IFeatureModule 实现
// （modules/），本类不承载具体功能逻辑；拔插功能 = 增删下方一处模块注册。
// 职责：
//   * 组装共享服务（热键注册器 / OCR 引擎 / 托盘菜单）到 AppServices
//   * 注册并装配全部功能模块（FeatureRegistry）
//   * 外壳自有托盘项（设置 / 关于 / 退出）
// ============================================================
class MainWindow : public QObject
{
    Q_OBJECT
public:
    explicit MainWindow(QObject *parent = nullptr);
    ~MainWindow() override; // shutdownAll：模块撤销能力槽，先于成员逆序析构

private slots:
    void onShowSettings(bool openCalendarPage = false);

private:
    // 更新自查：interactive=false 为启动静默检查（仅发现新版时提示），
    // true 为关于对话框手动检查（无更新/失败也反馈）
    void doUpdateCheck(bool interactive);

    QSystemTrayIcon *m_tray;
    QMenu *m_menu;
    QTimer *m_updateTimer = nullptr;    // 24h 定期更新自查（T-077）
    GlobalHotKey m_hotKey;              // 全局热键注册器（nativeEventFilter 由该类安装）
    RapidOcrService *m_ocr = nullptr;  // 共享 OCR 引擎（子进程昂贵，全局唯一）
    CaptureHistoryJob *m_captureHistory = nullptr; // 截图历史标注+清理（T-074）
    DesktopSurfaceHost *m_desktopSurface = nullptr; // 桌面表面（贴壁纸面板共用注入通道）
    AppServices *m_services = nullptr;  // 共享服务与贡献点
    FeatureRegistry m_registry;         // 功能模块注册表
};

} // namespace poptools
