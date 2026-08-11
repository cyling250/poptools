#include "OcrPanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFrame>

namespace snipaste {

OcrPanel::OcrPanel(QWidget *parent)
    : QWidget(parent)
{
    // 面板自身使用箭头光标（父窗口截图画布为十字/尺寸光标，
    // 若不在本控件显式设置，Qt 会沿祖先链继承父窗口光标）
    setCursor(Qt::ArrowCursor);

    // 卡片式外观：浅色半透明背景 + 边框，悬浮在截图画布上
    setStyleSheet(
        "OcrPanel, QTextEdit, QComboBox, QPushButton { background: rgba(255,255,255,238); }"
        "QTextEdit { border: 1px solid #c8c8c8; border-radius: 4px; color: #222;"
        "  padding: 3px; font-size: 13px; }"
        "QComboBox { border: 1px solid #c8c8c8; border-radius: 3px; padding: 1px 4px; }"
        "QPushButton { border: 1px solid #2f6feb; border-radius: 3px; color: #2f6feb;"
        "  padding: 2px 10px; font-size: 12px; }"
        "QPushButton:hover { background: #e6f2ff; }"
        "QPushButton:disabled { color: #999; border-color: #bbb; background: #f0f0f0; }"
        "QLabel { color: #555; font-size: 12px; }");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // 上半部分：OCR 识别到的文字（只读，多行）
    m_ocrEdit = new QTextEdit(this);
    m_ocrEdit->setReadOnly(true);
    m_ocrEdit->setPlaceholderText(QStringLiteral("OCR 识别文字"));
    m_ocrEdit->setMinimumHeight(90);
    layout->addWidget(m_ocrEdit, 1);

    // 中间：翻译选项（从 A 到 B）+ 翻译按钮
    auto *toolRow = new QHBoxLayout;
    toolRow->setSpacing(4);
    m_fromCombo = new QComboBox(this);
    const QStringList langs = {QStringLiteral("自动"), QStringLiteral("中文"), QStringLiteral("英文"),
                               QStringLiteral("日文"), QStringLiteral("韩文"), QStringLiteral("法文"),
                               QStringLiteral("德文"), QStringLiteral("西文"), QStringLiteral("俄文")};
    m_fromCombo->addItems(langs); // 源语言（A），默认"自动"由模型识别
    toolRow->addWidget(m_fromCombo);
    auto *arrow = new QLabel(QStringLiteral("→"), this);
    toolRow->addWidget(arrow);
    m_toCombo = new QComboBox(this);
    m_toCombo->setEditable(true);
    m_toCombo->addItems(langs.mid(1)); // 目标语言（B），不含"自动"
    m_toCombo->setCurrentText(QStringLiteral("中文")); // 默认目标语言由面板选择，不依赖设置
    toolRow->addWidget(m_toCombo, 1);
    m_translateBtn = new QPushButton(QStringLiteral("翻译"), this);
    m_translateBtn->setToolTip(QStringLiteral("将识别文字翻译为目标语言（需手动点击）"));
    toolRow->addWidget(m_translateBtn);
    layout->addLayout(toolRow);

    // 下半部分：翻译结果（只读）
    m_translateEdit = new QTextEdit(this);
    m_translateEdit->setReadOnly(true);
    m_translateEdit->setPlaceholderText(QStringLiteral("翻译结果"));
    m_translateEdit->setMinimumHeight(60);
    layout->addWidget(m_translateEdit, 1);

    // 翻译需用户手动点击按钮（默认不翻译）
    connect(m_translateBtn, &QPushButton::clicked, this, [this] {
        const QString text = m_ocrText.trimmed();
        if (!text.isEmpty())
            emit translateRequested(text, sourceLang(), targetLang());
    });
}

void OcrPanel::setOcrText(const QString &text)
{
    m_ocrText = text;
    m_ocrEdit->setPlainText(text);
}

void OcrPanel::setTranslateText(const QString &text)
{
    m_translateEdit->setPlainText(text);
}

QString OcrPanel::sourceLang() const
{
    const QString s = m_fromCombo->currentText().trimmed();
    if (s.contains(QStringLiteral("中")))
        return QStringLiteral("zh");
    if (s.contains(QStringLiteral("英")))
        return QStringLiteral("en");
    if (s.contains(QStringLiteral("日")))
        return QStringLiteral("ja");
    if (s.contains(QStringLiteral("韩")))
        return QStringLiteral("ko");
    if (s.contains(QStringLiteral("法")))
        return QStringLiteral("fr");
    if (s.contains(QStringLiteral("德")))
        return QStringLiteral("de");
    if (s.contains(QStringLiteral("西")))
        return QStringLiteral("es");
    if (s.contains(QStringLiteral("俄")))
        return QStringLiteral("ru");
    return QStringLiteral("auto"); // "自动"或未知 → 由模型识别
}

QString OcrPanel::targetLang() const
{
    const QString t = m_toCombo->currentText().trimmed();
    if (t.contains(QStringLiteral("中")))
        return QStringLiteral("zh");
    if (t.contains(QStringLiteral("英")))
        return QStringLiteral("en");
    if (t.contains(QStringLiteral("日")))
        return QStringLiteral("ja");
    if (t.contains(QStringLiteral("韩")))
        return QStringLiteral("ko");
    if (t.contains(QStringLiteral("法")))
        return QStringLiteral("fr");
    if (t.contains(QStringLiteral("德")))
        return QStringLiteral("de");
    if (t.contains(QStringLiteral("西")))
        return QStringLiteral("es");
    if (t.contains(QStringLiteral("俄")))
        return QStringLiteral("ru");
    return t; // 用户自定义（如 "pt"）
}

void OcrPanel::setBusy(bool busy)
{
    m_translateBtn->setDisabled(busy);
    m_translateBtn->setText(busy ? QStringLiteral("翻译中…") : QStringLiteral("翻译"));
}

} // namespace snipaste
