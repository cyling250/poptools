#pragma once

#include <QObject>
#include <QString>

namespace snipaste {

// ============================================================
// 划词翻译：获取 Windows 当前选中文本
//
// 职责：
//   * 保存当前剪贴板内容
//   * 模拟 Ctrl+C 复制选中文本到剪贴板
//   * 读取剪贴板文本后恢复原始内容
//   * 发出 textAcquired(text) 信号供外部调用翻译
//
// 线程安全：主线程调用（热键处理链），异步通过 QTimer 延迟读剪贴板。
// ============================================================
class SelectionTranslator : public QObject
{
    Q_OBJECT
public:
    explicit SelectionTranslator(QObject *parent = nullptr);

    // 触发获取选中文本（模拟 Ctrl+C → 延迟读剪贴板 → 恢复）
    void trigger();

signals:
    // 成功获取到选中文本
    void textAcquired(const QString &text);
    // 获取失败（无选中文本/剪贴板不可用等）
    void errorOccurred(const QString &error);

private slots:
    void onClipboardReady();

private:
    void saveClipboard();
    void restoreClipboard();
    void simulateCtrlC();

    QString m_savedText;       // 原始剪贴板文本（用于恢复）
    bool m_hasSavedText = false;
};

} // namespace snipaste