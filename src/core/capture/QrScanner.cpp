#include "QrScanner.h"

#ifdef POPTOOLS_HAVE_ZXING
// zxing-cpp 2.2.x：公共头文件平铺在 core/src（2.3+ 才有 ZXing/ 子目录）
#include <BarcodeFormat.h>
#include <DecodeHints.h>
#include <ReadBarcode.h>
#endif

namespace poptools {

namespace qr {

bool available()
{
#ifdef POPTOOLS_HAVE_ZXING
    return true;
#else
    return false;
#endif
}

QStringList scan(const QImage &image)
{
    QStringList out;
#ifdef POPTOOLS_HAVE_ZXING
    if (image.isNull())
        return out;
    // zxing ImageView 按内存字节序取像素：QImage::Format_ARGB32 的字节布局为
    // B,G,R,A —— 对应 ImageFormat::BGRX（第 4 字节 X 被忽略，无需预乘转换）
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);

    ZXing::DecodeHints hints;
    hints.setFormats(ZXing::BarcodeFormat::QRCode);
    hints.setTryHarder(true);
    hints.setTryRotate(true);
    const auto results = ZXing::ReadBarcodes(
        ZXing::ImageView(argb.bits(), argb.width(), argb.height(),
                         ZXing::ImageFormat::BGRX, argb.bytesPerLine()),
        hints);
    for (const auto &r : results)
    {
        // 兼容 zxing-cpp 2.x 的 text() 返回 string_view/string 两种形态
        const auto t = r.text();
        if (t.empty())
            continue;
        out << QString::fromUtf8(t.data(), static_cast<qsizetype>(t.size()));
    }
#endif
    return out;
}

} // namespace qr

} // namespace poptools
