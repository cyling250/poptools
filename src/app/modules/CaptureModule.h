#pragma once

#include "../IFeatureModule.h"

namespace poptools {

class AppServices;
class CaptureWindow;

// ============================================================
// 截图功能模块：CaptureWindow（画布式截图 / 标注 / OCR / 翻译面板）
// + F1 全局热键 + 托盘「截图」入口。
//
// 向 AppServices 发布的能力：
//   * captureActive / captureIntercept —— 截图态查询与"复制并关闭"拦截
//     （F3 贴图与划词翻译热键依此感知截图态）；
//   * lastCaptureRect —— 最近一次确认截图的选区（贴图定位用）。
// ============================================================
class CaptureModule : public IFeatureModule
{
public:
    QString id() const override { return QStringLiteral("capture"); }
    void init(AppServices &services) override;
    void shutdown() override; // 撤销发布的能力槽（防悬垂）

private:
    AppServices *m_services = nullptr;
    // 顶层窗口无父对象：进程退出由 OS 回收（析构不执行），故窗口内关键
    // 落盘/清理不得依赖析构（几何在确认截图时即时落盘，无析构依赖）
    CaptureWindow *m_window = nullptr;
};

} // namespace poptools
