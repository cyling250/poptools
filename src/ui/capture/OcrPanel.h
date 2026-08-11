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
// 截图模式下 OCR 识别的文字可附加到此面板（工具栏"面板"开关控制）：
//   * 上半部分：OCR 识别到的文字（只读）
//   * 中间：翻译选项（源语言 → 目标语言）+ 翻译按钮（默认不翻译，需手动点击）
//   * 下半部分：翻译结果（只读）
//
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

private:
    QTextEdit *m_ocrEdit = nullptr;
    QTextEdit *m_translateEdit = nullptr;
    QComboBox *m_fromCombo = nullptr; // 源语言（A）
    QComboBox *m_toCombo = nullptr;   // 目标语言（B）
    QPushButton *m_translateBtn = nullptr;
    QString m_ocrText;
};

} // namespace snipaste
