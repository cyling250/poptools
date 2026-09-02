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

private:
    AppServices *m_services = nullptr;
    CaptureWindow *m_window = nullptr; // 顶层窗口，随 QApplication 退出清理
};

} // namespace poptools
