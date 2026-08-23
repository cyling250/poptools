#include "WindowDetector.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#endif

namespace snipaste {

namespace {

#if defined(Q_OS_WIN)
// 系统窗口（桌面/任务栏等）不应作为检测对象
bool isSystemWindow(HWND hwnd)
{
    wchar_t cls[64] = {};
    if (!GetClassNameW(hwnd, cls, 64))
        return false;
    const std::wstring name(cls);
    return name == L"Progman" || name == L"WorkerW" ||
           name == L"Shell_TrayWnd" || name == L"Shell_SecondaryTrayWnd";
}

// 物理像素 → 逻辑坐标（右/下边界含端点 → 宽度 = right-left+1）
QRectF toLogicalWin(const RECT &r, qreal dpr)
{
    return QRectF(r.left / dpr, r.top / dpr,
                  (r.right - r.left + 1) / dpr, (r.bottom - r.top + 1) / dpr);
}

struct EnumAllCtx
{
    HWND exclude;              // 需排除的窗口（截图画布自身）
    qreal dpr;
    std::vector<QRect> *out;
};

BOOL CALLBACK enumAllProc(HWND hwnd, LPARAM lparam)
{
    auto *ctx = reinterpret_cast<EnumAllCtx *>(lparam);
    if (hwnd == ctx->exclude || !IsWindowVisible(hwnd))
        return TRUE;
    if (isSystemWindow(hwnd))
        return TRUE;
    RECT r{};
    if (!GetWindowRect(hwnd, &r))
        return TRUE;
    ctx->out->push_back(toLogicalWin(r, ctx->dpr).toRect());
    return TRUE;
}
#endif

// ---- 图像边缘识别（edge_det::detect_borders 的 OpenCV 移植）----

// 5 抽头高斯模糊 [1,4,6,4,1]/16（可分离，边界复制），输出 CV_32F
cv::Mat gaussianBlurF32(const cv::Mat &img)
{
    cv::Mat f;
    img.convertTo(f, CV_32F);
    cv::Mat k1 = (cv::Mat_<float>(1, 5) << 1.f, 4.f, 6.f, 4.f, 1.f) / 16.f;
    cv::Mat out;
    cv::sepFilter2D(f, out, CV_32F, k1, k1, cv::Point(-1, -1), 0, cv::BORDER_REPLICATE);
    return out;
}

// 梯度结果（幅值 + 方向分量 dx/dy）
struct Gradient
{
    cv::Mat mag, dx, dy;
};

// 灰度路：3x3 Sobel（与 edge_det::sobel 符号一致）
Gradient sobel3x3(const cv::Mat &src)
{
    cv::Mat gx, gy;
    cv::Sobel(src, gx, CV_32F, 1, 0, 3, 1.0, 0, cv::BORDER_CONSTANT);
    cv::Sobel(src, gy, CV_32F, 0, 1, 3, 1.0, 0, cv::BORDER_CONSTANT);
    Gradient g;
    g.dx = gx;
    g.dy = gy;
    cv::magnitude(gx, gy, g.mag);
    return g;
}

// NMS：沿梯度方向做双线性插值抑制（与 edge_det::nms 一致）
cv::Mat nms(const Gradient &g, int w, int h)
{
    cv::Mat out = cv::Mat::zeros(h, w, CV_32F);
    for (int y = 1; y < h - 1; ++y)
    {
        for (int x = 1; x < w - 1; ++x)
        {
            const float m = g.mag.at<float>(y, x);
            if (m < 0.5f)
                continue;
            const float gx = g.dx.at<float>(y, x);
            const float gy = g.dy.at<float>(y, x);
            const float agx = std::fabs(gx);
            const float agy = std::fabs(gy);
            float mag1 = 0.f, mag2 = 0.f;
            if (agx > agy)
            {
                const float t = agy / agx;
                if (gx * gy > 0.f)
                {
                    mag1 = g.mag.at<float>(y - 1, x - 1) * (1.f - t) + g.mag.at<float>(y - 1, x) * t;
                    mag2 = g.mag.at<float>(y + 1, x + 1) * (1.f - t) + g.mag.at<float>(y + 1, x) * t;
                }
                else
                {
                    mag1 = g.mag.at<float>(y - 1, x + 1) * (1.f - t) + g.mag.at<float>(y - 1, x) * t;
                    mag2 = g.mag.at<float>(y + 1, x - 1) * (1.f - t) + g.mag.at<float>(y + 1, x) * t;
                }
            }
            else if (agy > 0.f)
            {
                const float t = agx / agy;
                if (gx * gy > 0.f)
                {
                    mag1 = g.mag.at<float>(y - 1, x - 1) * (1.f - t) + g.mag.at<float>(y, x - 1) * t;
                    mag2 = g.mag.at<float>(y + 1, x + 1) * (1.f - t) + g.mag.at<float>(y, x + 1) * t;
                }
                else
                {
                    mag1 = g.mag.at<float>(y - 1, x + 1) * (1.f - t) + g.mag.at<float>(y, x + 1) * t;
                    mag2 = g.mag.at<float>(y + 1, x - 1) * (1.f - t) + g.mag.at<float>(y, x - 1) * t;
                }
            }
            if (m >= mag1 && m >= mag2)
                out.at<float>(y, x) = m;
        }
    }
    return out;
}

// RGB 颜色梯度：逐通道高斯后中央差，通道间幅值合成（edge_det::color_gradient）
Gradient colorGradient(const cv::Mat &img)
{
    std::vector<cv::Mat> ch(4);
    cv::split(img, ch);
    const cv::Mat rb = gaussianBlurF32(ch[0]);
    const cv::Mat gb = gaussianBlurF32(ch[1]);
    const cv::Mat bb = gaussianBlurF32(ch[2]);

    // 中央差：[ -1 0 1 ] 沿 x；[ -1; 0; 1 ] 沿 y
    const cv::Mat kx = (cv::Mat_<float>(1, 3) << -1.f, 0.f, 1.f);
    const cv::Mat ky = (cv::Mat_<float>(3, 1) << -1.f, 0.f, 1.f);
    cv::Mat drX, dgX, dbX, drY, dgY, dbY;
    const cv::Point anchor(-1, -1);
    cv::filter2D(rb, drX, CV_32F, kx, anchor, 0, cv::BORDER_REPLICATE);
    cv::filter2D(gb, dgX, CV_32F, kx, anchor, 0, cv::BORDER_REPLICATE);
    cv::filter2D(bb, dbX, CV_32F, kx, anchor, 0, cv::BORDER_REPLICATE);
    cv::filter2D(rb, drY, CV_32F, ky, anchor, 0, cv::BORDER_REPLICATE);
    cv::filter2D(gb, dgY, CV_32F, ky, anchor, 0, cv::BORDER_REPLICATE);
    cv::filter2D(bb, dbY, CV_32F, ky, anchor, 0, cv::BORDER_REPLICATE);

    cv::Mat sx2 = drX.mul(drX) + dgX.mul(dgX) + dbX.mul(dbX);
    cv::Mat sy2 = drY.mul(drY) + dgY.mul(dgY) + dbY.mul(dbY);
    cv::Mat sx, sy;
    cv::sqrt(sx2, sx);
    cv::sqrt(sy2, sy);

    // 方向符号 = 通道和的正负
    cv::Mat sumX = drX + dgX + dbX;
    cv::Mat sumY = drY + dgY + dbY;
    cv::Mat xge, yge;
    cv::compare(sumX, 0.0, xge, cv::CMP_GE);
    cv::compare(sumY, 0.0, yge, cv::CMP_GE);
    cv::Mat signX = cv::Mat::zeros(sx.size(), CV_32F);
    cv::Mat signY = cv::Mat::zeros(sy.size(), CV_32F);
    signX.setTo(1.f, xge);
    signX.setTo(-1.f, ~xge);
    signY.setTo(1.f, yge);
    signY.setTo(-1.f, ~yge);

    Gradient g;
    g.dx = sx.mul(signX);
    g.dy = sy.mul(signY);
    cv::magnitude(sx, sy, g.mag);
    return g;
}

// 双路合并 + 滞后阈值 + 8 邻域滞后连接（edge_det::merge_and_threshold + hysteresis）
cv::Mat thresholdHysteresis(const cv::Mat &graySup, const cv::Mat &colorSup,
                            int w, int h, double lo, double hi)
{
    cv::Mat labels = cv::Mat::zeros(h, w, CV_8U);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float v = std::max(graySup.at<float>(y, x), colorSup.at<float>(y, x));
            uchar &p = labels.at<uchar>(y, x);
            if (v >= hi)
                p = 2; // 强边缘
            else if (v >= lo)
                p = 1; // 弱边缘
        }

    std::vector<int> stack;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (labels.at<uchar>(y, x) == 2)
                stack.push_back(y * w + x);

    while (!stack.empty())
    {
        const int i = stack.back();
        stack.pop_back();
        const int x = i % w;
        const int y = i / w;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (dx == 0 && dy == 0)
                    continue;
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx < 0 || nx >= w || ny < 0 || ny >= h)
                    continue;
                uchar &q = labels.at<uchar>(ny, nx);
                if (q == 1)
                {
                    q = 2;
                    stack.push_back(ny * w + nx);
                }
            }
    }
    // 未连通的弱边缘丢弃
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (labels.at<uchar>(y, x) == 1)
                labels.at<uchar>(y, x) = 0;
    return labels;
}

// 连通域包围盒：按像素数过滤、按面积降序（edge_det::bounding_boxes）
std::vector<QRect> boundingBoxes(const cv::Mat &labels, int minArea)
{
    cv::Mat bin;
    cv::compare(labels, 2, bin, cv::CMP_EQ);
    cv::Mat out, stats, centroids;
    const int n = cv::connectedComponentsWithStats(bin, out, stats, centroids, 8);

    std::vector<QRect> boxes;
    for (int i = 1; i < n; ++i)
    {
        const int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area < minArea)
            continue;
        boxes.emplace_back(stats.at<int>(i, cv::CC_STAT_LEFT),
                           stats.at<int>(i, cv::CC_STAT_TOP),
                           stats.at<int>(i, cv::CC_STAT_WIDTH),
                           stats.at<int>(i, cv::CC_STAT_HEIGHT));
    }
    std::sort(boxes.begin(), boxes.end(), [](const QRect &a, const QRect &b) {
        return a.width() * a.height() > b.width() * b.height();
    });
    return boxes;
}

} // namespace

std::vector<QRect> detectImageBorders(const QImage &physImage,
                                      double lowThreshold, double highThreshold,
                                      int minArea)
{
    if (physImage.isNull())
        return {};
    if (physImage.width() <= 4 || physImage.height() <= 4)
        return {};

    // 统一为 RGBA 连续内存，适配 OpenCV Mat
    QImage rgba = physImage.convertToFormat(QImage::Format_RGBA8888);
    cv::Mat img(rgba.height(), rgba.width(), CV_8UC4,
                rgba.bits(), rgba.bytesPerLine());
    const int w = img.cols;
    const int h = img.rows;

    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_RGBA2GRAY);
    const Gradient graySobel = sobel3x3(gaussianBlurF32(gray));
    const cv::Mat graySup = nms(graySobel, w, h);

    const Gradient colorSob = colorGradient(img);
    const cv::Mat colorSup = nms(colorSob, w, h);

    const cv::Mat labels =
        thresholdHysteresis(graySup, colorSup, w, h, lowThreshold, highThreshold);
    return boundingBoxes(labels, minArea);
}

std::vector<QRect> detectRealWindows(void *excludeHwnd, qreal dpr)
{
    std::vector<QRect> out;
#if defined(Q_OS_WIN)
    if (dpr <= 0.0)
        dpr = 1.0;
    EnumAllCtx ctx;
    ctx.exclude = static_cast<HWND>(excludeHwnd);
    ctx.dpr = dpr;
    ctx.out = &out;
    EnumWindows(enumAllProc, reinterpret_cast<LPARAM>(&ctx));
#else
    Q_UNUSED(excludeHwnd)
    Q_UNUSED(dpr)
#endif
    return out;
}

} // namespace snipaste