#pragma once

#include <QWidget>

namespace poptools {
namespace desktopstack {

// ============================================================
// 贴桌窗口 Z 序协调器：贴在桌面层（Progman 正上方）的窗口分
// 三个层级，1s 全局一拍自底向上重排——
//
//   Progman < 日历层（CalendarWindow/MiniWidget） < 收纳盒层（BoxWindow）
//           < 悬浮球层（GoodyFloatingBall，T-050）
//
// 动机：日历与收纳盒各自的守护定时器都用"贴到 Progman 正上方"
// 锚定会互相抢位（Z 序横跳）；协调器让收纳盒层稳定在日历之上。
// 悬浮球体量最小且需常驻可点，放最顶层防被日历/收纳盒盖住。
//
// 用法：窗口 show 后 attach(this, Layer::X)，hide/edit 时 detach。
// 就位时每拍仅做一次 GetWindow 比较，零开销。
// ============================================================
enum class Layer
{
    Calendar = 0, // 桌面日历（月历卡片 + 迷你条）
    Box = 1,      // 桌面收纳盒
    Ball = 2      // Goody 悬浮球（T-050）
};

void attach(QWidget *w, Layer layer); // 注册 + 启动全局守护
void detach(QWidget *w);              // 注销（窗口隐藏/编辑模式时）

} // namespace desktopstack
} // namespace poptools
