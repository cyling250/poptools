#include "SelectionTranslator.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace poptools {

SelectionTranslator::SelectionTranslator(QObject *parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(100);
    connect(&m_pollTimer, &QTimer::timeout, this, &SelectionTranslator::onPollTick);
}

void SelectionTranslator::trigger()
{
#ifdef Q_OS_WIN
    // 记录复制前的剪贴板序列号，用于判断复制是否生效
    m_beforeSeq = GetClipboardSequenceNumber();
#else
    m_beforeSeq = 0;
#endif
    m_pollCount = 0;

    // 模拟 Ctrl+C 复制选中文本到剪贴板
    simulateCtrlC();

    // 轮询剪贴板序列号变化（比 Qt dataChanged 更直接，不受缓存影响）
    m_pollTimer.start();
}

void SelectionTranslator::onPollTick()
{
    if (++m_pollCount >= 25) { // 总超时约 2.5s
        fail();
        return;
    }

#ifdef Q_OS_WIN
    // 剪贴板未变化：约 600ms 时重发一次 Ctrl+C（首次注入可能未被目标应用处理）
    if (GetClipboardSequenceNumber() == m_beforeSeq) {
        if (m_pollCount == 6)
            simulateCtrlC();
        return;
    }
#endif

    // 剪贴板已变化：读取文本（可能是图片/清空，无文本则继续等待到超时）
    const QString text = readClipboardText();
    if (text.trimmed().isEmpty())
        return;

    finish(text);
}

void SelectionTranslator::finish(const QString &text)
{
    m_pollTimer.stop();
    emit textAcquired(text);
}

void SelectionTranslator::fail()
{
    m_pollTimer.stop();
    emit errorOccurred(QStringLiteral("未获取到选中文本"));
}

QString SelectionTranslator::readClipboardText() const
{
    QString text;
#ifdef Q_OS_WIN
    if (!OpenClipboard(nullptr))
        return text;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t *data = static_cast<const wchar_t *>(GlobalLock(h));
        if (data) {
            // 畸形剪贴板数据可能不含 NUL 结尾，fromWCharArray 无界读取会越界
            //（R3 #27）：以 GlobalSize 为字节上限找 NUL，找不到即按上限截断
            const SIZE_T maxChars = GlobalSize(h) / sizeof(wchar_t);
            int len = 0;
            while (size_t(len) < maxChars && data[len] != L'\0')
                ++len;
            text = QString::fromWCharArray(data, len);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
#endif
    return text;
}

void SelectionTranslator::simulateCtrlC()
{
#ifdef Q_OS_WIN
    // T-062 修复: 热键（如 Ctrl+Alt+Shift+X）触发瞬间用户仍物理按住 Alt/Shift/Win，
    // 直接注入 Ctrl+C 会被合成 Ctrl+Alt+Shift+C——多数应用未绑定该组合，剪贴板
    // 序列号不变，600ms 重发同样污染，最终 2.5s 超时报"未获取到选中文本"，
    // 表现为划词翻译时灵时不灵（P-046 修饰键残留教训在功能侧的落点）。
    // 处置：先对物理按住的修饰键注入 KEYEVENTF_KEYUP 再发 Ctrl+C。
    // 不回按（不恢复 keydown）：① 用户松手在即，回按与物理节奏不符；② 回按会
    // 在 Ctrl+C 之后向目标应用二次注入 Alt/Shift 按下——部分应用对 Alt 单独
    // 松开做菜单激活、Shift 按下改变后续输入语义，反而制造二次响应。
    // 物理按住期间 injected keyup 会被后续真实事件覆盖，且用户再松开时的
    // 真实 keyup 对已处于抬起状态的键无副作用
    INPUT inputs[9] = {}; // 至多 4 个修饰键 keyup + Ctrl/C 各按下抬起 4 个
    int n = 0;
    auto pushKeyup = [&inputs, &n](WORD vk) {
        inputs[n].type = INPUT_KEYBOARD;
        inputs[n].ki.wVk = vk;
        inputs[n].ki.dwFlags = KEYEVENTF_KEYUP;
        ++n;
    };
    if (GetAsyncKeyState(VK_MENU) & 0x8000)
        pushKeyup(VK_MENU);
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        pushKeyup(VK_SHIFT);
    // Win 键分左右两码，各自检测各自 keyup（未按下的那侧注入 keyup 无害）
    if (GetAsyncKeyState(VK_LWIN) & 0x8000)
        pushKeyup(VK_LWIN);
    if (GetAsyncKeyState(VK_RWIN) & 0x8000)
        pushKeyup(VK_RWIN);

    // Ctrl key down（Ctrl 本身是期望组合的一部分，无需先抬起）
    inputs[n].type = INPUT_KEYBOARD;
    inputs[n].ki.wVk = VK_CONTROL;
    ++n;

    // C key down
    inputs[n].type = INPUT_KEYBOARD;
    inputs[n].ki.wVk = 'C';
    ++n;

    // C key up
    inputs[n].type = INPUT_KEYBOARD;
    inputs[n].ki.wVk = 'C';
    inputs[n].ki.dwFlags = KEYEVENTF_KEYUP;
    ++n;

    // Ctrl key up
    inputs[n].type = INPUT_KEYBOARD;
    inputs[n].ki.wVk = VK_CONTROL;
    inputs[n].ki.dwFlags = KEYEVENTF_KEYUP;
    ++n;

    SendInput(n, inputs, sizeof(INPUT));
#else
    // 非 Windows 平台暂不支持
#endif
}

} // namespace poptools
