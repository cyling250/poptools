#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QPixmap>
#include <QRect>

#include "core/hotkey/GlobalHotKey.h"
#include "ui/capture/CaptureWindow.h"
#include "ui/result/ResultDialog.h"
#include "ui/pin/PinWindow.h"

namespace snipaste {

class OcrService;      // 前向声明（定义于 core/ocr）
class Translator;      // 前向声明（定义于 core/translation）

// ============================================================
// 应用控制器（无 UI 窗体）：系统托盘 + 全局热键 + 服务编排
//
// 职责：
//   * 注册全局热键（F1 截图 / F3 贴图，经 GlobalHotKey 封装）
//   * 组织 截图 → 结果面板 → OCR / 翻译 的完整流程
//   * 托盘菜单（截图 / 设置 / 关于 / 退出）
//   * F3 将最近一次截图（带批注）贴到屏幕顶层
// ============================================================
class MainWindow : public QObject
{
    Q_OBJECT
public:
    explicit MainWindow(QObject *parent = nullptr);

    // 热键 ID（注册到 GlobalHotKey）
    static constexpr int kCaptureHotKeyId = 1; // F1 截图
    static constexpr int kPinHotKeyId = 2;     // F3 贴图

private slots:
    void onCaptured(const QPixmap &pixmap, const QRect &selectionLogical);
    void onPinHotKey(); // F3：贴剪贴板最新图片（截图模式先复制+关闭；多张并存）
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
    GlobalHotKey m_hotKey;       // 全局热键（nativeEventFilter 由该类安装）
    QPixmap m_lastPixmap;        // 最近一次截图（带批注）
    QRect m_lastRect;            // 最近一次截图的选区位置（全局逻辑坐标）
};

} // namespace snipaste
