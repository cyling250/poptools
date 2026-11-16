#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <functional>

namespace poptools {

// ============================================================
// 收纳盒图标服务（复刻 T-054 清零前 core/box/IconExtractor 的成熟路线）：
// SHGetFileInfoW(SHGFI_SYSICONINDEX) → SHGetImageList(SHIL_JUMBO) →
// IImageList::GetIcon(ILD_TRANSPARENT)，回退 32px 大图标；HICON →
// GetIconInfo + GetDIBits 读 32bpp BGRA → ARGB32_Premultiplied（半透明
// 图标直接混合进共享位图，不预乘会发黑）。
// 红线 1：提取禁入主线程——QThreadPool 后台提取，回主线程入缓存。
// ============================================================
class IconService : public QObject
{
    Q_OBJECT
public:
    explicit IconService(QObject *parent = nullptr);

    // 请求图标（异步）：命中缓存立即同步回调；否则线程池提取后回主线程。
    // cb(target, image)：image 空表示提取失败（调用方画占位块）。
    // 失败也入缓存（负缓存）——否则渲染→请求→失败→重绘死循环会饿死主线程
    void requestIcon(const QString &target,
                     std::function<void(const QString &, const QImage &)> cb);

    // 渲染前查缓存（未请求返回空图且 contains() 为 false；已请求但失败返回
    // 空图且 contains() 为 true——调用方据此决定是否再请求）
    QImage cachedIcon(const QString &target) const;
    bool contains(const QString &target) const { return m_cache.contains(target); }

    // 提取线程函数（公开供单测直测：同步、阻塞、自带 COM 初始化）
    static QImage extractIcon(const QString &target);

private:
    QHash<QString, QImage> m_cache; // 主线程唯一访问（worker 只回传不入表）
    QSet<QString> m_inFlight;
};

} // namespace poptools
