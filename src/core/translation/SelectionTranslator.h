#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

namespace poptools {

// ============================================================
// 划词翻译：获取 Windows 当前选中文本
//
// 职责：
//   * 模拟 Ctrl+C 复制选中文本到剪贴板（选中内容保留在剪贴板中）
//   * 轮询 Windows 剪贴板序列号（GetClipboardSequenceNumber），
//     检测复制生效后直接用 Win32 API 读取文本
//   * 约 600ms 未生效时重发一次 Ctrl+C；总超时约 2.5s（复制未生效/无选中文本）
//     发出 errorOccurred
//
// 线程安全：主线程调用（热键处理链）；读取不依赖 Qt 剪贴板缓存，
// 避免信号/缓存时序导致读到旧内容。
// ============================================================
class SelectionTranslator : public QObject
{
    Q_OBJECT
public:
    explicit SelectionTranslator(QObject *parent = nullptr);

    // 触发获取选中文本（模拟 Ctrl+C → 轮询剪贴板序列号 → 读取）
    void trigger();

signals:
    // 成功获取到选中文本
    void textAcquired(const QString &text);
    // 获取失败（无选中文本/复制未生效等）
    void errorOccurred(const QString &error);

private slots:
    void onPollTick();

private:
    void simulateCtrlC();
    QString readClipboardText() const;
    void finish(const QString &text);
    void fail();

    QTimer m_pollTimer;      // 轮询剪贴板序列号
    quint32 m_beforeSeq = 0; // 复制前的剪贴板序列号
    int m_pollCount = 0;     // 轮询计数（重发/超时控制）
};

} // namespace poptools
