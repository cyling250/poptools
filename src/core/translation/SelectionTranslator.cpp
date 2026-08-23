#include "SelectionTranslator.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace snipaste {

SelectionTranslator::SelectionTranslator(QObject *parent)
    : QObject(parent)
{
}

void SelectionTranslator::trigger()
{
    // 保存当前剪贴板内容
    saveClipboard();

    // 模拟 Ctrl+C 复制选中文本到剪贴板
    simulateCtrlC();

    // 延迟等待剪贴板更新（注入按键后需一小段时间）
    QTimer::singleShot(50, this, &SelectionTranslator::onClipboardReady);
}

void SelectionTranslator::onClipboardReady()
{
    // 读取剪贴板文本
    const QString text = QGuiApplication::clipboard()->text();

    // 恢复原始剪贴板内容
    restoreClipboard();

    if (text.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("未获取到选中文本"));
    } else {
        emit textAcquired(text);
    }
}

void SelectionTranslator::saveClipboard()
{
    m_savedText = QGuiApplication::clipboard()->text();
    m_hasSavedText = !m_savedText.isEmpty();
}

void SelectionTranslator::restoreClipboard()
{
    if (m_hasSavedText) {
        QGuiApplication::clipboard()->setText(m_savedText);
    }
    m_hasSavedText = false;
    m_savedText.clear();
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