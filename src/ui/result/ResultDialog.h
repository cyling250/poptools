#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>

namespace snipaste {

// 结果面板：显示截图 + OCR 文本 + 翻译按钮
class ResultDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ResultDialog(QWidget *parent = nullptr);

    void setImage(const QPixmap &pix);
    void setOcrText(const QString &text);
    void setTranslateText(const QString &text);

private slots:
    void onOcrClicked();
    void onTranslateClicked();

signals:
    void ocrRequested();
    void translateRequested(const QString &text);

private:
    QLabel *m_imageLabel;
    QLabel *m_ocrLabel;
    QLabel *m_translateLabel;
    QPushButton *m_ocrBtn;
    QPushButton *m_translateBtn;
    QString m_ocrText;
};

} // namespace snipaste
