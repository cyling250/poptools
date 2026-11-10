#pragma once

#include <QHash>
#include <QPixmap>
#include <QString>

namespace poptools {

// ============================================================
// Dock 图标提取器：exe/.lnk 的系统 jumbo 图标（256px）+ 绘制型
// 资产图标（macOS 风格黄色文件夹 / 应用占位图）。
//
// 全部结果按 target 内存缓存（进程内），Dock 窗口绘制时直接取
// QPixmap 平滑缩放——不经过 QIcon（P-021：带 DPR 元数据的位图进
// QIcon 会在高 DPI 绘制路径被裁剪）。
// ============================================================
class IconExtractor
{
public:
    static IconExtractor &instance();

    // 应用图标：跟随 .lnk 目标提取 jumbo（256px）图标；提取失败返回
    // 占位图（圆角方块 + 首字符），保证 Dock 布局稳定
    QPixmap appIcon(const QString &target);

    // macOS 风格黄色文件夹图标（自绘，非系统文件夹图标）
    QPixmap folderIcon();

private:
    IconExtractor() = default;

    QPixmap extractShellIcon(const QString &target);
    QPixmap placeholderIcon(const QString &name);

    QHash<QString, QPixmap> m_appCache;
    QPixmap m_folderCache;
};

} // namespace poptools
