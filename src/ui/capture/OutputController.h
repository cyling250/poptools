#pragma once

#include <QPixmap>

#include "CaptureContext.h"

namespace poptools {

// ============================================================
// 输出控制器：按物理分辨率裁剪选区并合成标注与草稿
// （保存 / 复制 / 确认共用），提供保存与复制到剪贴板。
// ============================================================
class OutputController
{
public:
    explicit OutputController(CaptureContext *ctx) : m_ctx(ctx) {}

    QPixmap render() const;        // 合成结果图（物理分辨率，含标注与草稿）
    void save(QWidget *parent);    // 弹出保存对话框，成功后请求退出
    void copy();                   // 复制到剪贴板并请求退出

private:
    CaptureContext *m_ctx;
};

} // namespace poptools
