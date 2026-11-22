#pragma once

#include <QWidget>

namespace poptools {
namespace desktopstack {

// ============================================================
// 贴桌窗口 Z 序协调器：贴在桌面层（Progman 正上方）的窗口
// 按层级自底向上 1s 一拍重排——
//
//   Progman < 悬浮球层（DshFloatingBall）
//
// 动机：贴桌窗口用"贴到 Progman 正上方"锚定时会互相抢位
//（Z 序横跳）；协调器按层级统一归位。
//
// 用法：窗口 show 后 attach(this, Layer::X)，hide/edit 时 detach。
// 就位时每拍仅做一次 GetWindow 比较，零开销。
// ============================================================
enum class Layer
{
    Ball = 0  // DeepSeek Harness 悬浮球
};

void attach(QWidget *w, Layer layer); // 注册 + 启动全局守护
void detach(QWidget *w);              // 注销（窗口隐藏/编辑模式时）

} // namespace desktopstack
} // namespace poptools
