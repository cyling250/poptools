#pragma once

#include <QWidget>
#include <QString>

class QTextEdit;
class QComboBox;
class QPushButton;
class QShowEvent;

namespace poptools {

// ============================================================
// OCR 文字栏面板
//
// 截图模式下由【译】按钮触发打开，显示 OCR 原文与翻译结果：
//   * 上半部分：OCR 识别到的文字（只读）
//   * 中间：翻译选项（源语言 → 目标语言）+ 翻译按钮（面板内手动翻译）
//   * 下半部分：翻译结果（只读）
//   * 右上角：关闭按钮，关闭时隐藏面板并清除内容
//
// 跟随系统明暗主题（浅色=白底黑字，深色=黑底白字）。
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
    // T-062 删除了零接收者的 closed() 信号（全仓无 connect；Translator 也无
    // 可清的翻译缓存 API，关闭按钮 tooltip 的"清除翻译缓存"承诺落空）——
    // 将来若需缓存清理再补信号

protected:
    // 每次打开面板（新翻译任务）把源语言重置为"自动"：源语言不做记忆，
    // 始终交由模型自动检测（用户仍可在面板打开期间手动指定）
    void showEvent(QShowEvent *event) override;

private:
    // 应用当前系统明暗主题的整套样式（构造与 colorSchemeChanged 时调用）
    void applyTheme();
    QTextEdit *m_ocrEdit = nullptr;
    QTextEdit *m_translateEdit = nullptr;
    QComboBox *m_fromCombo = nullptr; // 源语言（A）
    QComboBox *m_toCombo = nullptr;   // 目标语言（B）
    QPushButton *m_translateBtn = nullptr;
    QPushButton *m_copyBtn = nullptr; // 复制翻译结果到剪贴板
    QPushButton *m_closeBtn = nullptr;
    QString m_ocrText;
};

} // namespace poptools
