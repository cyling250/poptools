#pragma once

#include "../IFeatureModule.h"

namespace poptools {

class AppServices;

// ============================================================
// 贴图功能模块：F3 全局热键把剪贴板最新图片张贴到屏幕顶层。
//
// 消费 CaptureModule 发布的能力：
//   * captureIntercept —— 截图进行中先复制当前截图并关闭（作为贴图来源），
//     同时取其选区作为张贴位置；
//   * lastCaptureRect —— 非截图态下的张贴位置回退。
// 贴图窗口不常驻：每次 F3 新建 PinWindow，关闭即销毁（WA_DeleteOnClose）。
// ============================================================
class PinModule : public IFeatureModule
{
public:
    QString id() const override { return QStringLiteral("pin"); }
    void init(AppServices &services) override;

private:
    void pinFromClipboard();

    AppServices *m_services = nullptr;
};

} // namespace poptools
