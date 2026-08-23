#pragma once

#include <QImage>
#include <QRect>
#include <vector>

namespace snipaste {

// ============================================================
// 窗口识别（eSearch「识别窗口」算法移植，OpenCV 实现）
//
//   1) detectImageBorders：图像边缘识别 —— 复刻 edge_det::detect_borders：
//      RGBA 像素 → 灰度 Sobel 与 RGB 颜色梯度两路 → NMS → 滞后阈值 → 连通域包围盒。
//      输入为物理像素截图，返回矩形包围盒（物理像素坐标，即图像坐标），按面积降序。
//   2) detectRealWindows：系统窗口枚举 —— 返回所有可见顶层窗口矩形（逻辑全局坐标）。
//
//   两者作为并列候选，由调用方（CaptureWindow）合并后在鼠标下方选择"最合适"区域以
//   预高亮/点选，替换原 MSAA + 最小窗口回退的检测逻辑。
// 非 Windows 平台时，系统窗口部分返回空向量。
// ============================================================

// 图像边缘识别：双路梯度（灰度 Sobel + RGB 颜色梯度）合并的 Canny 变体 + 连通域包围盒
// 默认阈值 50/150、minArea 3 与 eSearch「框选.自动框选.最小/最大阈值」一致
std::vector<QRect> detectImageBorders(const QImage &physImage,
                                      double lowThreshold = 50.0,
                                      double highThreshold = 150.0,
                                      int minArea = 3);

// 系统窗口枚举：所有可见顶层窗口（排除截图画布自身与系统窗口），物理→逻辑坐标
std::vector<QRect> detectRealWindows(void *excludeHwnd, qreal dpr);

} // namespace snipaste