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
    // 面板自身使用箭头光标
    setCursor(Qt::ArrowCursor);

    // 浅色背景，与工具栏面板风格一致
    setStyleSheet(
        "OcrPanel { background: #F5F5F5; border: 1px solid #D0D0D0; border-radius: 4px; }"
        "QTextEdit { background: #fff; border: 1px solid #D0D0D0; border-radius: 3px;"
        "  color: #1A1A1A; padding: 4px; font-size: 13px; }"
        "QTextEdit:focus { border-color: #2B6FF2; }"
        "QComboBox { background: #fff; border: 1px solid #D0D0D0; border-radius: 3px;"
        "  color: #1A1A1A; padding: 2px 4px; }"
        "QComboBox:focus { border-color: #2B6FF2; }"
        "QComboBox QAbstractItemView { background: #fff; color: #1A1A1A; }"
        "QPushButton { background: #fff; border: 1px solid #2B6FF2; border-radius: 3px;"
        "  color: #2B6FF2; padding: 2px 10px; font-size: 12px; }"
        "QPushButton:hover { background: #E8F0FE; }"
        "QPushButton:disabled { color: #8E8E93; border-color: #D0D0D0; background: #F0F0F0; }"
        "QLabel { color: #1A1A1A; font-size: 12px; }");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // 标题行：OCR 标签 + 关闭按钮
    auto *headerRow = new QHBoxLayout;
    auto *titleLabel = new QLabel(QStringLiteral("OCR 识别"), this);
    headerRow->addWidget(titleLabel);
    headerRow->addStretch(1);
    m_closeBtn = new QPushButton(QStringLiteral("✕"), this);
    m_closeBtn->setFixedSize(20, 20);
    m_closeBtn->setToolTip(QStringLiteral("关闭面板，清除翻译缓存"));
    m_closeBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; color: #8E8E93; font-size: 14px; }"
        "QPushButton:hover { color: #1A1A1A; }");
    headerRow->addWidget(m_closeBtn);
    layout->addLayout(headerRow);

    // 上半部分：OCR 识别到的文字（只读，多行）
    m_ocrEdit = new QTextEdit(this);
    m_ocrEdit->setReadOnly(true);
    m_ocrEdit->setPlaceholderText(QStringLiteral("OCR 识别文字"));
    m_ocrEdit->setMinimumHeight(90);
    layout->addWidget(m_ocrEdit, 1);

    // 区段分隔线
    auto *sectionSep = new QFrame(this);
    sectionSep->setFrameShape(QFrame::HLine);
    sectionSep->setStyleSheet("QFrame { color: #E0E0E0; }");
    layout->addWidget(sectionSep);

    // 中间：翻译选项（从 A 到 B）+ 翻译按钮
    auto *toolRow = new QHBoxLayout;
    toolRow->setSpacing(4);
    m_fromCombo = new QComboBox(this);
    const QStringList langs = {QStringLiteral("自动"), QStringLiteral("中文"), QStringLiteral("英文"),
                               QStringLiteral("日文"), QStringLiteral("韩文"), QStringLiteral("法文"),
                               QStringLiteral("德文"), QStringLiteral("西文"), QStringLiteral("俄文")};
    m_fromCombo->addItems(langs);
    toolRow->addWidget(m_fromCombo);
    auto *arrow = new QLabel(QStringLiteral("→"), this);
    toolRow->addWidget(arrow);
    m_toCombo = new QComboBox(this);
    m_toCombo->setEditable(true);
    m_toCombo->addItems(langs.mid(1));
    m_toCombo->setCurrentText(QStringLiteral("中文"));
    toolRow->addWidget(m_toCombo, 1);
    m_translateBtn = new QPushButton(QStringLiteral("翻译"), this);
    m_translateBtn->setToolTip(QStringLiteral("将识别文字翻译为目标语言（需手动点击）"));
    toolRow->addWidget(m_translateBtn);
    layout->addLayout(toolRow);

    // 区段分隔线
    auto *sectionSep2 = new QFrame(this);
    sectionSep2->setFrameShape(QFrame::HLine);
    sectionSep2->setStyleSheet("QFrame { color: #E0E0E0; }");
    layout->addWidget(sectionSep2);

    // 下半部分：翻译结果（只读）
    m_translateEdit = new QTextEdit(this);
    m_translateEdit->setReadOnly(true);
    m_translateEdit->setPlaceholderText(QStringLiteral("翻译结果"));
    m_translateEdit->setMinimumHeight(60);
    layout->addWidget(m_translateEdit, 1);

    // 翻译按钮：手动触发
    connect(m_translateBtn, &QPushButton::clicked, this, [this] {
        const QString text = m_ocrText.trimmed();
        if (!text.isEmpty())
            emit translateRequested(text, sourceLang(), targetLang());
    });

    // 关闭按钮：隐藏面板 + 清除内容 + 通知外部清理翻译缓存
    connect(m_closeBtn, &QPushButton::clicked, this, [this] {
        m_ocrText.clear();
        m_ocrEdit->clear();
        m_translateEdit->clear();
        hide();
        emit closed();
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
    return QStringLiteral("auto");
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
    return t;
}

void OcrPanel::setBusy(bool busy)
{
    m_translateBtn->setDisabled(busy);
    m_translateBtn->setText(busy ? QStringLiteral("翻译中…") : QStringLiteral("翻译"));
}

} // namespace snipaste