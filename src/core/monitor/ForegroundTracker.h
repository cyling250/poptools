#pragma once

#include <QObject>
#include <QString>

namespace poptools {

// ============================================================
// 前台窗口跟踪：EVENT_SYSTEM_FOREGROUND 系统事件钩子。
//
// 与 LL 钩子同一纪律：回调（WinEventProc）只做入队，进程名解析
//（OpenProcess/QueryFullProcessImageNameW）与标题读取挪回主线程
// 事件循环的槽中完成。start 时补发一次当前前台窗口，保证任何输入
// 之前都有窗口上下文。
// ============================================================
class ForegroundTracker : public QObject
{
    Q_OBJECT
public:
    explicit ForegroundTracker(QObject *parent = nullptr);
    ~ForegroundTracker() override;

    bool isRunning() const { return m_active; }
    void start();
    void stop();

signals:
    // 前台窗口变化（同 hwnd 只在标题变化时重发）
    void foregroundChanged(quintptr hwnd, const QString &app, const QString &title);

private slots:
    void onForegroundWindow(qlonglong hwnd);

private:
    bool m_active = false;
    quintptr m_lastHwnd = 0;
    QString m_lastTitle;
};

} // namespace poptools
