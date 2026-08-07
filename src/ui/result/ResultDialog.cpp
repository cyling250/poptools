#include "ResultDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QClipboard>

namespace snipaste {

ResultDialog::ResultDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("MySnipaste - 截图结果");
    setMinimumSize(560, 480);

    auto *layout = new QVBoxLayout(this);

    // 截图预览
    m_imageLabel = new QLabel(this);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setMinimumHeight(160);
    layout->addWidget(m_imageLabel);

    // OCR 文本
    auto *ocrGroup = new QGroupBox("OCR 识别文本", this);
    auto *ocrLayout = new QVBoxLayout(ocrGroup);
    m_ocrLabel = new QLabel("（尚未识别）", ocrGroup);
    m_ocrLabel->setWordWrap(true);
    m_ocrLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ocrLayout->addWidget(m_ocrLabel);
    layout->addWidget(ocrGroup);

    // 翻译结果
    auto *trGroup = new QGroupBox("翻译结果", this);
    auto *trLayout = new QVBoxLayout(trGroup);
    m_translateLabel = new QLabel("（尚未翻译）", trGroup);
    m_translateLabel->setWordWrap(true);
    m_translateLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    trLayout->addWidget(m_translateLabel);
    layout->addWidget(trGroup);

    // 按钮行
    auto *btnLayout = new QHBoxLayout;
    m_ocrBtn = new QPushButton("识别文字 (OCR)", this);
    m_translateBtn = new QPushButton("翻译", this);
    m_translateBtn->setEnabled(false);
    auto *copyBtn = new QPushButton("复制全部", this);
    auto *closeBtn = new QPushButton("关闭", this);
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
