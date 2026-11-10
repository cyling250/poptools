#include "ImeCommitDetector.h"

#include <QDateTime>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace poptools {

namespace {
constexpr qint64 kLetterWindowMs = 3000; // 字母键的"组合期"有效窗口
} // namespace

ImeCommitDetector::ImeCommitDetector(QObject *parent)
    : QObject(parent)
{
}

qint64 ImeCommitDetector::now() const
{
    return m_now ? m_now() : QDateTime::currentMSecsSinceEpoch();
}

bool ImeCommitDetector::imeGate()
{
    if (m_imeGate)
        return m_imeGate();
#ifdef Q_OS_WIN
    // 前台窗口线程的键盘布局：CJK 输入法激活时 HKL 高 16 位语言 ID 的
    // 主语言为 zh(0x04)/ja(0x11)/ko(0x12)；英文布局（0x09）为 false
    const HWND fg = GetForegroundWindow();
    DWORD tid = 0;
    if (fg)
        tid = GetWindowThreadProcessId(fg, nullptr);
    const HKL hkl = GetKeyboardLayout(tid);
    const BYTE primary = static_cast<BYTE>((reinterpret_cast<quintptr>(hkl) >> 16) & 0xFF);
    return primary == 0x04 || primary == 0x11 || primary == 0x12;
#else
    return false;
#endif
}

void ImeCommitDetector::reset()
{
    m_letterAtMs = 0;
}

void ImeCommitDetector::onKeyEvent(int vk, bool down)
{
    if (!down)
        return;

    // 字母键（A-Z）：记住时刻，构成"组合期"证据
    if (vk >= 0x41 && vk <= 0x5A) {
        m_letterAtMs = now();
        return;
    }

    // 候选确认键：空格（首选）/ 数字 1-9（备选）/ 回车（上屏原文）
    const bool commitKey = vk == VK_SPACE || vk == VK_RETURN || (vk >= '1' && vk <= '9');
    if (!commitKey)
        return;

    const qint64 nowMs = now();
    if (m_letterAtMs == 0 || nowMs - m_letterAtMs > kLetterWindowMs)
        return; // 近期没敲过字母：纯英文空格/回车，不是上屏
    if (!imeGate())
        return; // 前台非 CJK 布局：英文打字的空格

    emit commitDetected();
}

} // namespace poptools
