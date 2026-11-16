#include "BoxPanel.h"

#include "BoxAdjustOverlay.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QPainter>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace poptools {

BoxPanel::BoxPanel(BoxCatalog *catalog, const QString &boxId, QObject *parent)
    : QObject(parent)
    , m_boxId(boxId)
    , m_catalog(catalog)
    , m_icons(new IconService(this))
    , m_watcher(new QFileSystemWatcher(this))
{
    // 绑定目录内容变化 → 盒视图自动同步（文件夹门户语义核心）。
    // 防抖：资源管理器批量操作（解压/多文件粘贴）会连发 directoryChanged，
    // 逐条重渲染会形成风暴——200ms 合并一次
    m_refreshDebounce.setInterval(200);
    m_refreshDebounce.setSingleShot(true);
    connect(&m_refreshDebounce, &QTimer::timeout, this, [this] { refreshEntries(); });
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString &) { m_refreshDebounce.start(); });
    refreshEntries();
}

BoxPanel::~BoxPanel() = default;

const BoxDef BoxPanel::def() const
{
    static const BoxDef kEmpty;
    const int i = m_catalog ? m_catalog->indexOf(m_boxId) : -1;
    return i >= 0 ? m_catalog->boxes().at(i) : kEmpty;
}

QRect BoxPanel::rectPhysical() const
{
    // 隐藏盒不参与命中与 DLL 槽位失效（空矩形 → 表面宿主清槽 + hitPanel 跳过）
    if (def().hidden)
        return QRect();
    return m_layout.has_value() ? m_layout->panel : QRect();
}

QImage *BoxPanel::frame()
{
    return m_staging.isNull() ? nullptr : &m_staging;
}

void BoxPanel::onDutyStarted()
{
    if (!m_surface || !m_surface->canvas().isValid())
        return;
    if (!m_renderer) {
        const QRect screen = m_surface->screenPhysical();
        m_renderer = std::make_unique<BoxRenderer>(screen.width(), screen.height(),
                                                   m_surface->dpr());
    }
    if (!m_wallpaperTimer) {
        m_wallpaperTimer = new QTimer(this);
        m_wallpaperTimer->setInterval(30000);
        connect(m_wallpaperTimer, &QTimer::timeout, this, [this] {
            if (m_renderer)
                m_renderer->refreshWallpaper();
            render();
        });
    }
    m_wallpaperTimer->start();
    refreshEntries();
}

void BoxPanel::onDutyStopped()
{
    if (m_wallpaperTimer)
        m_wallpaperTimer->stop();
}

void BoxPanel::reloadDef()
{
    // 定义（隐藏/位置/列数/页）被管理器或托盘改过：整帧重算重绘
    refreshEntries();
}

void BoxPanel::refreshEntries()
{
    const BoxDef d = def();
    m_items.clear();
    if (!d.dir.isEmpty() && QDir(d.dir).exists()) {
        const QFileInfoList infos =
            QDir(d.dir).entryInfoList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot,
                                      QDir::Name | QDir::LocaleAware);
        m_items.reserve(infos.size());
        for (const QFileInfo &fi : infos) {
            BoxItem item;
            item.name = fi.fileName();
            item.target = fi.absoluteFilePath();
            item.isDir = fi.isDir();
            m_items.append(item);
        }
        // watcher 跟踪绑定目录（目录被删后重建时重新挂上）
        const QStringList watched = m_watcher->directories();
        if (!watched.contains(d.dir)) {
            if (!watched.isEmpty())
                m_watcher->removePaths(watched);
            m_watcher->addPath(d.dir);
        }
    } else if (!m_watcher->directories().isEmpty()) {
        m_watcher->removePaths(m_watcher->directories());
    }
    m_selected = -1;
    render();
}

void BoxPanel::render()
{
    renderFrame(QRect());
}

void BoxPanel::renderFrame(const QRect &commitDirty)
{
    if (m_adjusting || !m_renderer || !m_surface || !m_surface->canvas().isValid())
        return;
    const BoxDef d = def();
    QImage &canvasImage = m_surface->canvas().image();
    if (m_staging.size() != canvasImage.size() || m_staging.format() != canvasImage.format())
        m_staging = QImage(canvasImage.size(), canvasImage.format());
    // 尺寸纪律（T-060）：行数随条目自动增高（不超屏），超容量翻页；面板宽 =
    // 列数×格子宽恒为整数倍
    const BoxLayout::Sizing sz = BoxLayout::compute(QPoint(d.x, d.y), d.cols, m_items.size(),
                                                    m_surface->screenPhysical());
    const BoxLayout layout(sz.panel, d.cols, m_items.size(), d.page);
    ensureVisibleIcons(layout.capacity, layout.page);
    QPainter g(&m_staging);
    m_layout = m_renderer->render(&g, d, layout, m_items, m_hover, m_selected, m_iconCache);
    g.end();
    // 悬停这类只动了格子/头部底色的高频小改动带脏矩形提交，免整屏合成发布
    if (commitDirty.isEmpty())
        m_surface->commitSurface();
    else
        m_surface->commitSurface(commitDirty);
}

void BoxPanel::renderDuringAdjust()
{
    if (!m_renderer || !m_surface || !m_surface->canvas().isValid())
        return;
    const BoxDef d = def();
    QImage &canvasImage = m_surface->canvas().image();
    if (m_staging.size() != canvasImage.size() || m_staging.format() != canvasImage.format())
        m_staging = QImage(canvasImage.size(), canvasImage.format());
    const BoxLayout::Sizing sz = BoxLayout::compute(QPoint(d.x, d.y), d.cols, m_items.size(),
                                                    m_surface->screenPhysical());
    const BoxLayout layout(sz.panel, d.cols, m_items.size(), d.page);
    ensureVisibleIcons(layout.capacity, layout.page);
    QPainter g(&m_staging);
    const QPoint off(-9999, -9999);
    m_layout = m_renderer->render(&g, d, layout, m_items, off, -1, m_iconCache);
    g.end();
    m_surface->commitSurface();
}

void BoxPanel::ensureVisibleIcons(int capacity, int page)
{
    const int first = page * capacity;
    const int remaining = m_items.size() - first;
    const int visible = std::min(capacity, remaining > 0 ? remaining : 0);
    for (int i = 0; i < visible; ++i) {
        const QString &target = m_items.at(first + i).target;
        if (m_icons->contains(target))
            continue; // 已请求过（含失败负缓存）——失败画占位块，永不重试
        m_icons->requestIcon(target, [this](const QString &t, const QImage &img) {
            // IconService 负缓存保证回调只发生一次；仅成功提取才值得重绘
            //（失败条目永远是占位块，重绘它就是浪费主线程）
            if (!img.isNull()) {
                m_iconCache.insert(t, img);
                render();
            }
        });
    }
    // 缓存裁剪：只留当前盒条目引用的键，防只增不减
    QSet<QString> live;
    for (const BoxItem &it : m_items)
        live.insert(it.target);
    for (auto it = m_iconCache.begin(); it != m_iconCache.end();) {
        if (!live.contains(it.key()))
            it = m_iconCache.erase(it);
        else
            ++it;
    }
}

void BoxPanel::onMove(const QPoint &p)
{
    if (!eventsAccepted())
        return;
    updateHover(p);
}

void BoxPanel::onLeave()
{
    if (!eventsAccepted())
        return;
    if (m_hover != QPoint(-9999, -9999) || m_lastHoverCell != -1 || m_lastHoverHeader != -1) {
        const QRect dirty = hoverVisualRect(m_lastHoverCell, m_lastHoverHeader);
        m_hover = QPoint(-9999, -9999);
        m_lastHoverCell = -1;
        m_lastHoverHeader = -1;
        renderFrame(dirty);
    }
}

void BoxPanel::onClick(const QPoint &p)
{
    if (!eventsAccepted())
        return;
    handleSingleClick(p);
}

void BoxPanel::onDoubleClick(const QPoint &p)
{
    if (!eventsAccepted())
        return;
    if (!m_layout.has_value())
        return;
    const int idx = m_layout->entryIndexAt(p);
    if (idx >= 0)
        launchEntry(idx);
}

QPoint BoxPanel::logicalPos(const QPoint &physical) const
{
    const qreal dpr = m_surface ? m_surface->dpr() : 1.0;
    if (dpr <= 0.0)
        return physical;
    return QPoint(qRound(physical.x() / dpr), qRound(physical.y() / dpr));
}

void BoxPanel::updateHover(const QPoint &p)
{
    m_hover = p;
    if (!m_layout.has_value())
        return;
    // 悬停语义目标（格子/头部按钮下标）变化才重绘：格内滑动零重绘，跨格恰好
    // 一次。T-061：旧实现只区分“热区内外”——格→格不重绘，高亮滞留旧格、
    // 滑出网格才迟到出现，正是“不顺滑/有时不出现”的根因
    const int cell = m_layout->hitCell(p);
    const int header = m_layout->hitHeader(p);
    if (cell == m_lastHoverCell && header == m_lastHoverHeader)
        return;
    const QRect dirty = hoverVisualRect(m_lastHoverCell, m_lastHoverHeader)
                            .united(hoverVisualRect(cell, header));
    m_lastHoverCell = cell;
    m_lastHoverHeader = header;
    renderFrame(dirty);
}

QRect BoxPanel::hoverVisualRect(int cell, int header) const
{
    if (!m_layout.has_value())
        return QRect();
    if (cell >= 0 && cell < m_layout->cells.size())
        return m_layout->cells.at(cell);
    if (header >= 0 && header < m_layout->headerButtons.size())
        return m_layout->headerButtons.at(header).adjusted(-1, -1, 1, 1); // 悬停底外扩
    return QRect();
}

QRect BoxPanel::selectedVisualRect(int globalIndex) const
{
    if (!m_layout.has_value() || globalIndex < 0)
        return QRect();
    const int cell = globalIndex - m_layout->page * m_layout->capacity;
    if (cell < 0 || cell >= m_layout->cells.size())
        return QRect();
    return m_layout->cells.at(cell);
}

void BoxPanel::handleSingleClick(const QPoint &p)
{
    if (!m_layout.has_value())
        return;
    const int hh = m_layout->hitHeader(p);
    if (hh >= 0) {
        BoxDef d = def();
        switch (BoxHeaderKind(hh)) {
            case BoxHeaderKind::Prev:
                if (m_layout->page > 0) {
                    d.page = m_layout->page - 1;
                    m_catalog->updateBox(m_boxId, d);
                    m_selected = -1;
                    render();
                }
                break;
            case BoxHeaderKind::Next:
                if (m_layout->page < m_layout->pageCount - 1) {
                    d.page = m_layout->page + 1;
                    m_catalog->updateBox(m_boxId, d);
                    m_selected = -1;
                    render();
                }
                break;
            case BoxHeaderKind::Adjust:
                enterAdjustMode();
                break;
        }
        return;
    }
    const int dot = m_layout->hitDot(p);
    if (dot >= 0 && dot != m_layout->page) {
        BoxDef d = def();
        d.page = dot;
        m_catalog->updateBox(m_boxId, d);
        m_selected = -1;
        render();
        return;
    }
    const int idx = m_layout->entryIndexAt(p);
    if (idx >= 0) {
        if (m_selected != idx) {
            // 单击：仅选中（Windows 桌面习惯，双击才启动）；高亮只在新旧
            // 格子矩形内变化，走脏矩形提交
            const QRect dirty =
                selectedVisualRect(m_selected).united(selectedVisualRect(idx));
            m_selected = idx;
            renderFrame(dirty);
        }
        return;
    }
    if (m_selected != -1) {
        const QRect dirty = selectedVisualRect(m_selected); // 点空白清除选中
        m_selected = -1;
        renderFrame(dirty);
    }
}

void BoxPanel::launchEntry(int index)
{
    if (index < 0 || index >= m_items.size())
        return;
    // 用户主动点击：文件与子文件夹都交给系统默认方式打开（门户语义）
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_items.at(index).target));
}

void BoxPanel::enterAdjustMode()
{
    if (m_adjusting || !m_surface || !m_layout.has_value())
        return;
    m_adjusting = true;
    const BoxDef original = def();
    // 调整语义（T-060 用户定版）：内部拖动 = 移动位置；右缘拖动 = 改列数；
    // 尺寸永远格子整数倍（宽 = 列数×格子宽，高随条目自动），无自由缩放。
    // 虚线框每帧从 rectPhysical() 回取（列数变了面板重排，框实时跟随）
    auto *overlay = new BoxAdjustOverlay(
        m_layout->panel, m_surface->screenPhysical(), m_surface->dpr(),
        [this](const QPoint &topLeft, int cols) {
            BoxDef d = def();
            d.x = topLeft.x();
            d.y = topLeft.y();
            d.cols = cols;
            m_catalog->updateBox(m_boxId, d); // 实时落盘（拖动中，量小无碍）
            renderDuringAdjust();
        },
        [this](const QPoint &topLeft, int cols) {
            BoxDef d = def();
            d.x = topLeft.x();
            d.y = topLeft.y();
            d.cols = cols;
            m_catalog->updateBox(m_boxId, d);
        },
        [this, original]() {
            if (m_catalog)
                m_catalog->updateBox(m_boxId, original);
        },
        [this]() { return rectPhysical(); },
        nullptr);
    connect(overlay, &QObject::destroyed, this, [this] {
        m_adjusting = false;
        render();
    });
    overlay->show();
    overlay->raise();
    overlay->activateWindow();
}

} // namespace poptools
