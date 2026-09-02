#include "PinModule.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QGuiApplication>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QTimer>

#include "../AppServices.h"
#include "ui/pin/PinWindow.h"

namespace snipaste {

void PinModule::init(AppServices &services)
{
    m_services = &services;

    // F3：贴剪贴板最新图片。热键在原生事件过滤器中触发：UI 操作（窗口创建/激活）
    // 延迟到事件循环，避免时序问题
    services.registerHotKey(0x72 /* VK_F3 */, 0, [this] {
        QTimer::singleShot(0, QCoreApplication::instance(),
                           [this] { pinFromClipboard(); });
    });
}

void PinModule::pinFromClipboard()
{
    // 截图模式：先复制当前截图（含标注）并关闭截图模式（作为"剪贴板最新内容"），
    // 同时取本次选区位置作为张贴位置
    QRect captureSel;
    if (m_services->captureIntercept)
        captureSel = m_services->captureIntercept();

    // 从剪贴板取最新图片张贴；剪贴板为文字时 pixmap 为空 → 无响应
    const QPixmap img = QGuiApplication::clipboard()->pixmap();
    if (img.isNull())
        return;

    // 张贴位置：本次截图位置 > 最近一次确认截图位置 > 鼠标当前位置（作为左上角点）
    QPoint pos;
    if (!captureSel.isEmpty())
        pos = captureSel.topLeft();
    else if (!m_services->lastCaptureRect.isEmpty())
        pos = m_services->lastCaptureRect.topLeft();
    else
        pos = QCursor::pos();

    // 多张贴图并存：每次 F3 创建新实例，互不影响；关闭时（WA_DeleteOnClose）自动销毁
    auto *pin = new PinWindow();
    pin->pin(img, pos);
}

} // namespace snipaste
