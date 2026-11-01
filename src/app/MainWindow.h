#pragma once

#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>

#include "FeatureRegistry.h"
#include "core/hotkey/GlobalHotKey.h"

namespace poptools {

class AppServices;
class PaddleOcrService;

// ============================================================
// 应用外壳（组合根）：系统托盘 + 共享服务装配 + 功能模块注册。
//
// 各功能（截图 / 贴图 / 划词翻译）以 IFeatureModule 实现（modules/），
// 本类不承载具体功能逻辑；拔插功能 = 增删下方一处模块注册。
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
    void onShowSettings();

private:
    QSystemTrayIcon *m_tray;
    QMenu *m_menu;
    GlobalHotKey m_hotKey;              // 全局热键注册器（nativeEventFilter 由该类安装）
    PaddleOcrService *m_ocr = nullptr;  // 共享 OCR 引擎（子进程昂贵，全局唯一）
    AppServices *m_services = nullptr;  // 共享服务与贡献点
    FeatureRegistry m_registry;         // 功能模块注册表
};

} // namespace poptools
