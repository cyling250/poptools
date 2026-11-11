#include "DesktopStack.h"

#include <QCoreApplication>
#include <QPointer>
#include <QTimer>
#include <QVector>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace poptools {
namespace desktopstack {

namespace {
// 各层注册表（QPointer 防悬挂：窗口析构后自动变空并清除）
// T-050：新增 Ball 层（悬浮球），层表随之扩到 3
constexpr int kLayerCount = 3;
// T-050-FIX（P3-3）：层表条数与 Layer 枚举平行维护，编译期锁死防漏改
static_assert(int(Layer::Ball) == kLayerCount - 1,
              "kLayerCount must track desktopstack::Layer (Ball is topmost)");
QVector<QPointer<QWidget>> g_layers[kLayerCount];
QTimer *g_timer = nullptr;

// 自底向上重排：Progman → 日历层 → 收纳盒层 → 悬浮球层。
// 已就位的窗口零开销（仅一次 GetWindow 比较）。
void assertOrder()
{
#ifdef Q_OS_WIN
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman)
        return;
    HWND prev = progman;
    for (int layer = 0; layer < kLayerCount; ++layer) {
        for (const QPointer<QWidget> &w : g_layers[layer]) {
            if (!w || !w->isVisible())
                continue;
            const HWND hwnd = reinterpret_cast<HWND>(w->winId());
            if (GetWindow(hwnd, GW_HWNDNEXT) != prev) {
                SetWindowPos(hwnd, prev, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            prev = hwnd;
        }
    }
#endif
}
} // namespace

void attach(QWidget *w, Layer layer)
{
    if (!w)
        return;
    QVector<QPointer<QWidget>> &list = g_layers[int(layer)];
    if (!list.contains(w))
        list.append(w);

    if (!g_timer) {
        g_timer = new QTimer(QCoreApplication::instance());
        g_timer->setInterval(1000);
        QObject::connect(g_timer, &QTimer::timeout, &assertOrder);
    }
    assertOrder(); // 立即归位一次
    if (!g_timer->isActive())
        g_timer->start();
}

void detach(QWidget *w)
{
    for (auto &list : g_layers)
        list.removeOne(w);
    // 全部注销时停拍（避免空转计时器）
    const bool any = std::any_of(std::begin(g_layers), std::end(g_layers),
                                 [](const QVector<QPointer<QWidget>> &l) {
                                     return !l.isEmpty();
                                 });
    if (!any && g_timer)
        g_timer->stop();
}

} // namespace desktopstack
} // namespace poptools
