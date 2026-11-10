#include "InputActivityMonitor.h"

#include <QDateTime>

namespace poptools {

namespace {
constexpr qint64 kCheckpointMs = 60 * 1000; // 同窗长会话切片周期
}

InputActivityMonitor::InputActivityMonitor(QObject *parent)
    : QObject(parent)
{
}

qint64 InputActivityMonitor::now() const
{
    return m_now ? m_now() : QDateTime::currentMSecsSinceEpoch();
}

void InputActivityMonitor::setWindowContext(quintptr hwnd, const QString &app,
                                            const QString &title)
{
    if (hwnd == m_lastHwnd) {
        // 同一顶层窗口（浏览器换标签/编辑器换文件）：标题只更新不切段，
        // 段记录的是"停留窗口"， finer 粒度交给 keys/ocrtext 流
        m_lastTitle = title;
        if (m_hasCur)
            m_cur.title = title;
        return;
    }
    closeSegment(false);
    m_lastHwnd = hwnd;
    m_lastApp = app;
    m_lastTitle = title;
    // 不立即开段：无输入的窗口不计时长（路过≠工作）
}

void InputActivityMonitor::noteKeydown()
{
    if (!m_hasCur)
        openSegment();
    ++m_cur.keystrokes;
    m_lastInputMs = now();
}

void InputActivityMonitor::noteClick()
{
    if (!m_hasCur)
        openSegment();
    ++m_cur.clicks;
    m_lastInputMs = now();
}

void InputActivityMonitor::tick()
{
    if (!m_hasCur)
        return;
    const qint64 now = this->now();
    if (now - m_lastInputMs > m_idleThresholdMs) {
        closeSegment(true); // end=最后一次输入时刻：不把发呆计入时长
        return;
    }
    ++m_cur.activeSec;
    if (now - m_cur.startMs >= kCheckpointMs) {
        closeSegment(false);
        openSegment(); // 同窗续段（上下文未变）
    }
}

void InputActivityMonitor::flush()
{
    closeSegment(false);
}

void InputActivityMonitor::openSegment()
{
    m_cur = TimelineSegment{};
    m_cur.app = m_lastApp;
    m_cur.title = m_lastTitle;
    m_cur.startMs = now();
    m_hasCur = true;
}

void InputActivityMonitor::closeSegment(bool idleEnded)
{
    if (!m_hasCur)
        return;
    m_cur.endMs = idleEnded ? m_lastInputMs : now();
    if (m_cur.endMs < m_cur.startMs)
        m_cur.endMs = m_cur.startMs;
    m_cur.idleEnded = idleEnded;
    emit segmentClosed(m_cur);
    m_hasCur = false;
}

} // namespace poptools
