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

// Win32 启发式探测（样式+类名），结果按 hwnd 缓存于调用方
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

// 容量护栏：超 64 条整体清空（不逐条按 TTL 淘汰——焦点控件本来就高频更替，
// 过期条目下次探测自然 miss）。（T-062 B-7 登记）：UIA 正判条目被驱逐即回退
// Win32 启发式——对启发式探测不到的自绘密码框（见 cacheUiaPassword 注释），
// 正判条目在 500ms TTL 窗口内被驱逐会提前失去保护（键入流 setMasked 与 OCR
// 门卫共用本缓存，ActivityMonitorModule 接线）；TTL 本就限定正判保护时长，
// 驱逐只是可能提前这一回退。OCR 链另有读取期 UIA IsPassword 精确复查兜底
void evictIfNeeded()
{
    if (g_cache.size() > 64)
        g_cache.clear();
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
    evictIfNeeded();
    g_cache.insert(hwnd, qMakePair(pwd, now));
    return pwd;
}

void cacheUiaPassword(quintptr hwnd, bool pwd)
{
    // T-047-UIA：UIA 精确判定回写共享缓存（仅主线程调用——onUiaFinished
    // 在 GUI 线程）。T-047-FIX(P3-5) 注释修正：仅收正判——调用方只在 UIA
    // 判定为密码框时才回写（pwd 恒为 true），负判不回写是有意保守（防
    // UIA 负判误开打码豁免）；正判覆盖 Win32 启发式探测不到的自绘密码框
    if (!hwnd)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    evictIfNeeded();
    g_cache.insert(hwnd, qMakePair(pwd, now));
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

void cacheUiaPassword(quintptr hwnd, bool pwd)
{
    Q_UNUSED(hwnd);
    Q_UNUSED(pwd);
}
#endif

} // namespace PasswordFieldGuard

} // namespace poptools
