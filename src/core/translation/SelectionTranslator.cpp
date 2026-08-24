#include "SelectionTranslator.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace snipaste {

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
            text = QString::fromWCharArray(data);
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
    INPUT inputs[4] = {};

    // Ctrl key down
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;

    // C key down
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'C';

    // C key up
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'C';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

    // Ctrl key up
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(4, inputs, sizeof(INPUT));
#else
    // 非 Windows 平台暂不支持
#endif
}

} // namespace snipaste
