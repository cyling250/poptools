#include "KeyboardTextComposer.h"

#include <QDateTime>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace poptools {

KeyboardTextComposer::KeyboardTextComposer(QObject *parent)
    : QObject(parent)
{
#ifdef Q_OS_WIN
    // 修饰键初值对齐系统实时状态（监控启动时 Shift 可能已被按住）
    m_shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    m_ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    m_alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    m_caps = (GetAsyncKeyState(VK_CAPITAL) & 0x0001) != 0;
#endif
}

void KeyboardTextComposer::setWindowContext(const QString &app, const QString &title)
{
    if (app == m_app && title == m_title)
        return;
    flush();
    m_app = app;
    m_title = title;
}

void KeyboardTextComposer::onKeyEvent(int vk, int scan, bool extended, bool down)
{
    Q_UNUSED(extended);

#ifdef Q_OS_WIN
    switch (vk) {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        m_shift = down; return;
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        m_ctrl = down; return;
    case VK_MENU: case VK_LMENU: case VK_RMENU:
        m_alt = down; return;
    case VK_CAPITAL:
        if (down) m_caps = !m_caps; // CapsLock 是开关态（down 翻转一次）
        return;
    default: break;
    }
    if (!down)
        return; // 其余键只关心按下

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastInputMs = now;

    if (vk == VK_BACK) {
        // 退格近似：从缓冲尾删一字符（文档真实状态以 OCR 流为准）
        if (!m_text.isEmpty())
            m_text.chop(1);
        return;
    }
    if (vk == VK_RETURN) {
        // 回车通常意味着输入完成（提交/换行），冲刷成独立一行事件
        m_text += QLatin1Char('\n');
        flush();
        return;
    }

    // Ctrl/Alt 组合键是快捷键而非"写内容"，不记录
    if (m_ctrl || m_alt)
        return;

    if (m_maskPasswords && m_maskedNow) {
        // 密码段：只记占位符，长度近似等于按键数（含自动重复）
        notePrintable(QChar(0x25CF)); // ●
        return;
    }

    QChar ch;
    if (vk == VK_SPACE)
        ch = QLatin1Char(' ');
    else if (vk == VK_TAB)
        ch = QLatin1Char('\t');
    else if (!translateVk(vk, scan, ch))
        return; // 功能键/死键/翻译失败：跳过

    notePrintable(ch);
#endif
}

void KeyboardTextComposer::onIdleTick(qint64 nowMs)
{
    if (!m_text.isEmpty() && nowMs - m_lastInputMs >= m_idleFlushMs)
        flush();
}

void KeyboardTextComposer::flush()
{
    if (m_text.isEmpty())
        return;
    emit textReady(m_app, m_title, m_text, m_runMasked);
    m_text.clear();
    m_runMasked = false;
}

void KeyboardTextComposer::notePrintable(const QChar &c)
{
    m_text += c;
    if (m_maskPasswords && m_maskedNow)
        m_runMasked = true;
    if (m_text.size() >= m_maxRunChars)
        flush();
}

// vk → 字符：按前台窗口线程的键盘布局翻译（多布局用户在两种输入法间
// 切换时各自正确）。使用合成状态数组调用 ToUnicodeEx，不污染系统
// 键盘状态（该函数可能修改传入数组，我们每次重建，无副作用）
bool KeyboardTextComposer::translateVk(int vk, int scan, QChar &out) const
{
#ifdef Q_OS_WIN
    const HWND fg = GetForegroundWindow();
    DWORD tid = 0;
    if (fg)
        tid = GetWindowThreadProcessId(fg, nullptr);
    const HKL hkl = GetKeyboardLayout(tid);

    BYTE state[256] = {};
    if (m_shift) state[VK_SHIFT] = 0x80;
    if (m_ctrl) state[VK_CONTROL] = 0x80;
    if (m_alt) state[VK_MENU] = 0x80;
    if (m_caps) state[VK_CAPITAL] = 0x01;

    wchar_t buf[8] = {};
    const int n = ToUnicodeEx(static_cast<UINT>(vk), static_cast<UINT>(scan), state,
                              buf, 8, 0, hkl);
    if (n == 1 && buf[0] >= 0x20) { // 可打印字符；死键返回 2 或 -1，跳过
        out = QChar(buf[0]);
        return true;
    }
#else
    Q_UNUSED(vk); Q_UNUSED(scan); Q_UNUSED(out);
#endif
    return false;
}

} // namespace poptools
