#pragma once

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>

#include "CaptureWindow.h"
#include "OcrService.h"
#include "Translator.h"

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

// 主窗口（无 UI 窗体）：系统托盘 + 全局热键（默认 F1 截图）
class MainWindow : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit MainWindow(QObject *parent = nullptr);
    ~MainWindow() override;

    bool registerHotKeys();

protected:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private slots:
    void onCaptured(const QPixmap &pixmap);
    void onOcrRequested();
    void onTranslateRequested(const QString &text);
    void onOcrFinished(bool ok, const QString &text, const QString &error);
    void onTranslateFinished(bool ok, const QString &result, const QString &error);
    void onShowSettings();

private:
    QSystemTrayIcon *m_tray;
    QMenu *m_menu;
    CaptureWindow *m_capture;
    OcrService *m_ocr;
    Translator *m_translator;
    ResultDialog *m_result;
    QPixmap m_lastPixmap;
    quint32 m_hotkeyId = 1;
    bool m_hotkeyRegistered = false;
};
