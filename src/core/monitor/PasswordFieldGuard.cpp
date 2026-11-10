#include "PasswordFieldGuard.h"

#include <QDateTime>
#include <QHash>
#include <QPair>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace poptools {

namespace PasswordFieldGuard {

#ifdef Q_OS_WIN
namespace {

constexpr qint64 kCacheTtlMs = 500;

// hwnd → (是否密码框, 探测时刻)：同控件连打时跳过重复探测
QHash<quintptr, QPair<bool, qint64>> g_cache;

bool probePassword(HWND hwnd)
{
    if (!hwnd)
        return false;

    // Win32 Edit：ES_PASSWORD 样式即密码框（覆盖绝大多数登录框）
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (style & ES_PASSWORD)
        return true;

    // 兜底：类名自报家门（部分自绘控件带 password/secret 字样）
    wchar_t cls[128] = {};
    GetClassNameW(hwnd, cls, 128);
    const QString className = QString::fromWCharArray(cls).toLower();
    return className.contains(QLatin1String("password"))
           || className.contains(QLatin1String("secret"));
}

} // namespace

bool isPasswordFocused()
{
    GUITHREADINFO gi = {};
    gi.cbSize = sizeof(gi);
    // idThread=0 → 前台线程：hwndFocus 是该线程的焦点控件
    if (!GetGUIThreadInfo(0, &gi))
        return false;
    return isPasswordWindow(reinterpret_cast<quintptr>(gi.hwndFocus));
}

bool isPasswordWindow(quintptr hwnd)
{
    // 针对捕获时刻记录的焦点窗口判定（打码判定挪回捕获点：钩子回调
    // 随事件带焦点 HWND，样式探测仍在主线程处理时刻做——控件密码样式
    // 极少变化，迟到的探测不改变结论）
    if (!hwnd)
        return false;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto it = g_cache.constFind(hwnd);
    if (it != g_cache.cend() && now - it->second < kCacheTtlMs)
        return it->first;

    const bool pwd = probePassword(reinterpret_cast<HWND>(hwnd));
    if (g_cache.size() > 64)
        g_cache.clear(); // 简单防膨胀：焦点控件本来就高频更替
    g_cache.insert(hwnd, qMakePair(pwd, now));
    return pwd;
}
#else
bool isPasswordFocused()
{
    return false;
}

bool isPasswordWindow(quintptr hwnd)
{
    Q_UNUSED(hwnd);
    return false;
}
#endif

} // namespace PasswordFieldGuard

} // namespace poptools
