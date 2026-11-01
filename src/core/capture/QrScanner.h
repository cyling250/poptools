#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

namespace poptools {

// ============================================================
// 二维码识别（zxing-cpp 后端）。
//
// 依赖以 green 套件形式存在：tools/zxing（scripts/install_zxing.py 下载），
// CMake 检测到时定义 POPTOOLS_HAVE_ZXING 并静态编入；缺失时 scan()
// 恒返回空、available() 为 false，功能提示"组件未安装"，不阻断构建。
// ============================================================
namespace qr {

// 识别组件是否可用（zxing-cpp 是否已编入）
bool available();

// 识别图中的二维码（支持一图多码），返回文本列表（UTF-8）；无码/不可用返回空。
// image 任意格式均可（内部统一转 ARGB32）
QStringList scan(const QImage &image);

} // namespace qr

} // namespace poptools
