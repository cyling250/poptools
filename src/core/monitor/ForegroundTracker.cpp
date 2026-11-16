#include "ForegroundTracker.h"

#include <QDateTime>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace poptools {

#ifdef Q_OS_WIN
namespace {

ForegroundTracker *g_tracker = nullptr;
HWINEVENTHOOK g_eventHook = nullptr;

// WinEvent 回调：只入队（同 LL 钩子纪律）
void CALLBACK winEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject,
                           LONG idChild, DWORD, DWORD)
{
    if (event == EVENT_SYSTEM_FOREGROUND && idObject == OBJID_WINDOW && idChild == 0
        && hwnd && g_tracker) {
        QMetaObject::invokeMethod(g_tracker, "onForegroundWindow", Qt::QueuedConnection,
                                  Q_ARG(qlonglong, static_cast<qlonglong>(
                                                       reinterpret_cast<quintptr>(hwnd))));
    }
}

// 进程名（不含路径）：OpenProcess 有限查询不需要提权
QString exeBaseName(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid)
        return QString();
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return QString();
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD size = MAX_PATH * 2;
    QString name;
    if (QueryFullProcessImageNameW(h, 0, buf, &size))
        name = QFileInfo(QString::fromWCharArray(buf)).completeBaseName().toLower();
    CloseHandle(h);
    return name;
}

QString windowTitle(HWND hwnd)
{
    wchar_t buf[512] = {};
    const int n = GetWindowTextW(hwnd, buf, 512);
    return QString::fromWCharArray(buf, n);
}

} // namespace
#endif

// 无状态：全部资源在 start 建立
ForegroundTracker::ForegroundTracker(QObject *parent)
    : QObject(parent)
{
}

// 析构即 stop：卸 WinEvent 钩子
ForegroundTracker::~ForegroundTracker()
{
    stop();
}

void ForegroundTracker::start()
{
#ifdef Q_OS_WIN
    // T-062 修复: 单实例防重入由 Q_ASSERT 改运行期防御（对齐
    // GlobalKeyboardHook R6-C4 口径）——Release 构建断言失效，第二个实例
    // start 会不卸旧钩子直接覆盖 g_eventHook（旧钩泄漏）且新旧两钩同时
    // 回调造成前台事件翻倍。已有消费者时拒绝激活并告警
    if (g_tracker && g_tracker != this) {
        qWarning("ForegroundTracker: 已有其他消费者 %p，拒绝二次启动",
                 static_cast<void *>(g_tracker));
        return;
    }
#endif
    if (m_active)
        return;
#ifdef Q_OS_WIN
    g_tracker = this;
    // WINEVENT_OUTOFCONTEXT：回调投递到安装线程（主线程）的消息循环
    g_eventHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                  nullptr, winEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (!g_eventHook) {
        // 装钩失败（无障碍服务被禁/安全策略等，P2-10）：回滚为未激活并
        // 告警——静默失败会让时间线与键入文本全无前台上下文（app/title
        // 恒空）且无从排查（对照 GlobalKeyboardHook 的失败回退）
        g_tracker = nullptr;
        qWarning("ForegroundTracker: SetWinEventHook(EVENT_SYSTEM_FOREGROUND) 失败 err=%lu",
                 GetLastError());
        return;
    }
    m_active = true;
    // 启动即补发当前前台窗口（首个输入之前就有上下文）
    if (HWND fg = GetForegroundWindow()) {
        QMetaObject::invokeMethod(this, "onForegroundWindow", Qt::QueuedConnection,
                                  Q_ARG(qlonglong,
                                        static_cast<qlonglong>(reinterpret_cast<quintptr>(fg))));
    }
#else
    m_active = true;
#endif
}

void ForegroundTracker::stop()
{
    if (!m_active)
        return;
    m_active = false;
#ifdef Q_OS_WIN
    if (g_tracker == this)
        g_tracker = nullptr;
    if (g_eventHook) {
        UnhookWinEvent(g_eventHook);
        g_eventHook = nullptr;
    }
#endif
}

// 前台事件落点（主线程队列）：读标题、同窗同标题去重（Alt+Tab 回自己不重发），
// 变化则解析进程名并转发。进程名/标题读取在此完成而非钩子回调
//（WinEventProc 只入队纪律）
void ForegroundTracker::onForegroundWindow(qlonglong hwnd)
{
#ifdef Q_OS_WIN
    const HWND h = reinterpret_cast<HWND>(static_cast<quintptr>(hwnd));
    if (!h)
        return;
    const QString title = windowTitle(h);
    if (h == reinterpret_cast<HWND>(m_lastHwnd) && title == m_lastTitle)
        return; // 去重：同窗同标题（Alt+Tab 回自己等）
    m_lastHwnd = reinterpret_cast<quintptr>(h);
    m_lastTitle = title;
    emit foregroundChanged(m_lastHwnd, exeBaseName(h), title);
#else
    Q_UNUSED(hwnd);
#endif
}

} // namespace poptools
