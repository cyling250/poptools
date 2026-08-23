#pragma once

#include <QWidget>
#include <QString>

class QTextEdit;
class QComboBox;
class QPushButton;

namespace snipaste {

// ============================================================
// OCR 文字栏面板
//
// 截图模式下由【译】按钮触发打开，显示 OCR 原文与翻译结果：
//   * 上半部分：OCR 识别到的文字（只读）
//   * 中间：翻译选项（源语言 → 目标语言）+ 翻译按钮（面板内手动翻译）
//   * 下半部分：翻译结果（只读）
//   * 右上角：关闭按钮，关闭时清除面板内容并发出 closed() 信号
//
// 始终使用黑色文字 + 白色背景，不受系统主题影响。
// 宽度至少 10 字符、位置（外部/内部）由 CaptureWindow 定位。
// ============================================================
class OcrPanel : public QWidget
{
    Q_OBJECT
public:
    explicit OcrPanel(QWidget *parent = nullptr);

    void setOcrText(const QString &text);      // 上半：OCR 文字
    void setTranslateText(const QString &text);// 下半：翻译结果
    QString sourceLang() const;                // 当前选择的源语言（A，"auto"=自动识别）
    QString targetLang() const;                // 当前选择的目标语言（B）
    void setBusy(bool busy);                   // 翻译进行中：禁用按钮

signals:
    // 用户点击翻译按钮（需主动触发，默认不翻译）
    void translateRequested(const QString &text, const QString &fromLang, const QString &toLang);
    // 面板被关闭（关闭按钮点击），用于通知外部清理翻译缓存
    void closed();

private:
    QTextEdit *m_ocrEdit = nullptr;
    QTextEdit *m_translateEdit = nullptr;
    QComboBox *m_fromCombo = nullptr; // 源语言（A）
    QComboBox *m_toCombo = nullptr;   // 目标语言（B）
    QPushButton *m_translateBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QString m_ocrText;
};

} // namespace snipaste
