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

ForegroundTracker::ForegroundTracker(QObject *parent)
    : QObject(parent)
{
}

ForegroundTracker::~ForegroundTracker()
{
    stop();
}

void ForegroundTracker::start()
{
    if (m_active)
        return;
    m_active = true;
#ifdef Q_OS_WIN
    Q_ASSERT(!g_tracker && "ForegroundTracker 目前只支持单实例");
    g_tracker = this;
    // WINEVENT_OUTOFCONTEXT：回调投递到安装线程（主线程）的消息循环
    g_eventHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                  nullptr, winEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    // 启动即补发当前前台窗口（首个输入之前就有上下文）
    if (HWND fg = GetForegroundWindow()) {
        QMetaObject::invokeMethod(this, "onForegroundWindow", Qt::QueuedConnection,
                                  Q_ARG(qlonglong,
                                        static_cast<qlonglong>(reinterpret_cast<quintptr>(fg))));
    }
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
