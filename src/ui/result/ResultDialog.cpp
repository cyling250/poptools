#include "ResultDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QClipboard>
#include <QFrame>
#include <QLabel>

namespace snipaste {

ResultDialog::ResultDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("MySnipaste - 截图结果");
    setMinimumSize(560, 480);
    setStyleSheet(
        "QDialog { background: #F5F5F5; }"
        "QLabel.section-title { font-weight: bold; color: #1A1A1A; font-size: 13px;"
        "  padding: 8px 0 4px 0; }"
        "QLabel.content { color: #1A1A1A; font-size: 13px;"
        "  background: #fff; border: 1px solid #D0D0D0; border-radius: 3px;"
        "  padding: 8px; min-height: 40px; }");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(0);

    auto addSection = [&](const QString &title) {
        auto *label = new QLabel(title, this);
        label->setProperty("class", "section-title");
        label->setStyleSheet("QLabel { font-weight: bold; color: #1A1A1A; font-size: 13px;"
                             "  padding: 8px 0 4px 0; }");
        layout->addWidget(label);
    };

    auto addSep = [&]() {
        auto *sep = new QFrame(this);
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet("QFrame { color: #E0E0E0; margin: 6px 0; }");
        layout->addWidget(sep);
    };

    // 截图预览
    addSection("截图预览");
    m_imageLabel = new QLabel(this);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setMinimumHeight(160);
    m_imageLabel->setStyleSheet(
        "QLabel { background: #fff; border: 1px solid #D0D0D0; border-radius: 3px;"
        "  padding: 4px; }");
    layout->addWidget(m_imageLabel);

    addSep();

    // OCR 文本
    addSection("OCR 识别文本");
    m_ocrLabel = new QLabel("（尚未识别）", this);
    m_ocrLabel->setWordWrap(true);
    m_ocrLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_ocrLabel->setStyleSheet(
        "QLabel { color: #1A1A1A; font-size: 13px;"
        "  background: #fff; border: 1px solid #D0D0D0; border-radius: 3px;"
        "  padding: 8px; min-height: 40px; }");
    layout->addWidget(m_ocrLabel);

    addSep();

    // 翻译结果
    addSection("翻译结果");
    m_translateLabel = new QLabel("（尚未翻译）", this);
    m_translateLabel->setWordWrap(true);
    m_translateLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_translateLabel->setStyleSheet(
        "QLabel { color: #1A1A1A; font-size: 13px;"
        "  background: #fff; border: 1px solid #D0D0D0; border-radius: 3px;"
        "  padding: 8px; min-height: 40px; }");
    layout->addWidget(m_translateLabel);

    layout->addStretch();

    // 按钮行
    auto *btnLayout = new QHBoxLayout;
    btnLayout->setContentsMargins(0, 8, 0, 0);
    btnLayout->setSpacing(6);
    m_ocrBtn = new QPushButton("识别文字 (OCR)", this);
    m_ocrBtn->setStyleSheet(
        "QPushButton { background: #fff; border: 1px solid #2B6FF2; border-radius: 3px;"
        "  color: #2B6FF2; padding: 5px 14px; }"
        "QPushButton:hover { background: #E8F0FE; }");
    m_translateBtn = new QPushButton("翻译", this);
    m_translateBtn->setEnabled(false);
    m_translateBtn->setStyleSheet(
        "QPushButton { background: #fff; border: 1px solid #D0D0D0; border-radius: 3px;"
        "  color: #1A1A1A; padding: 5px 14px; }"
        "QPushButton:hover { background: #E8F0FE; border-color: #2B6FF2; color: #2B6FF2; }"
        "QPushButton:disabled { color: #8E8E93; border-color: #D0D0D0; background: #F0F0F0; }");
    auto *copyBtn = new QPushButton("复制全部", this);
    copyBtn->setStyleSheet(
        "QPushButton { background: #2B6FF2; color: #fff; border: 1px solid #2B6FF2;"
        "  border-radius: 3px; padding: 5px 14px; }"
        "QPushButton:hover { background: #1A5CDB; }");
    auto *closeBtn = new QPushButton("关闭", this);
    closeBtn->setStyleSheet(
        "QPushButton { background: #fff; border: 1px solid #D0D0D0; border-radius: 3px;"
        "  color: #1A1A1A; padding: 5px 14px; }"
        "QPushButton:hover { background: #E8F0FE; border-color: #2B6FF2; color: #2B6FF2; }");
    btnLayout->addWidget(m_ocrBtn);
    btnLayout->addWidget(m_translateBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(copyBtn);
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);

    connect(m_ocrBtn, &QPushButton::clicked, this, &ResultDialog::onOcrClicked);
    connect(m_translateBtn, &QPushButton::clicked, this, &ResultDialog::onTranslateClicked);
    connect(copyBtn, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_ocrText + "\n\n" + m_translateLabel->text());
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void ResultDialog::setImage(const QPixmap &pix)
{
    m_imageLabel->setPixmap(pix.scaled(m_imageLabel->size(), Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
}

void ResultDialog::setOcrText(const QString &text)
{
    m_ocrText = text;
    m_ocrLabel->setText(text.isEmpty() ? "（识别失败或无文本）" : text);
    m_translateBtn->setEnabled(!text.isEmpty());
}

void ResultDialog::setTranslateText(const QString &text)
{
    m_translateLabel->setText(text.isEmpty() ? "（尚未翻译）" : text);
}

void ResultDialog::onOcrClicked()
{
    emit ocrRequested();
}

void ResultDialog::onTranslateClicked()
{
    emit translateRequested(m_ocrText);
}

} // namespace snipaste
