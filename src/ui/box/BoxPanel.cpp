#include "BoxPanel.h"

#include "BoxAdjustOverlay.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QPainter>
#include <QTimer>
#include <QUrl>

namespace poptools {

// 一盒一面板：挂目录 watcher + 200ms 防抖重列；构造即首列（值守未启时 render
// 走空守卫，不会画到无效画布）
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

// T-062 修复: overlay 是无 parent 的独立顶层窗，四个回调全部捕获 this（BUG-03）——
// 面板被 deleteLater（管理器增删改触发 rebuildPanels）后 overlay 若仍存活，
// 下一次 paintEvent → rectProvider() 即悬空调用崩溃。hide 先掐断 paintEvent
//（不再触 rectProvider），再排入销毁队列；overlay→panel 方向的 destroyed 连接
// 以 panel 为接收者上下文，panel 析构时自动断连，安全
BoxPanel::~BoxPanel()
{
    if (m_overlay) {
        m_overlay->hide();
        m_overlay->deleteLater();
    }
}

// 当前盒定义副本（按 m_boxId 查仓库）；id 已被管理器解绑/仓库为空时返回空 Def
//（x/y 为默认值）——调用方按空盒渲染而非崩溃。返回值拷贝，勿存引用
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

// DesktopPanel：面板私有暂存帧；未渲染过返回 nullptr（宿主合成跳过）
QImage *BoxPanel::frame()
{
    return m_staging.isNull() ? nullptr : &m_staging;
}

// 值守开启：懒建渲染器（此时才有画布尺寸/DPR）+ 30s 壁纸刷新定时器 + 重列首绘。
// 幂等（宿主可能补发）
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

// 值守停止：仅停壁纸定时器。stopDuty 不销毁画布/renderer，commitSurface 也不检查
// 值守态——停值守后目录 watcher 的防抖重列仍会照常重绘并发布（注入端已移除，
// 无消费方；paintFrame 的守卫只拦从未开启过值守的早期动作，与基线行为一致）
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

// DesktopPanel：整帧重绘 + 整帧提交（commitSurface 空脏矩形）
void BoxPanel::render(bool force /*对收纳盒无差异*/)
{
    Q_UNUSED(force)
    renderFrame(QRect());
}

void BoxPanel::renderFrame(const QRect &commitDirty)
{
    if (m_adjusting)
        return; // 调整模式由 renderDuringAdjust 专职绘制，常规路径避让
    paintFrame(m_hover, m_selected, commitDirty);
}

// 调整模式实时预览：悬停/选中强制抹掉（哨兵/-1，overlay 语义下无意义）；整帧提交
// ——面板矩形一直在变，脏矩形无从谈起
void BoxPanel::renderDuringAdjust()
{
    paintFrame(kHoverOff, -1, QRect());
}

// 返回 false = 守卫拦截（无渲染器/画布无效）。hoverPoint/selectedIndex 为本次
// 绘制的语义目标；commitDirty 空 = 整帧提交。语句序列与求值顺序与合并前两条
// 路径逐行一致（S-07 提取，行为等价）
bool BoxPanel::paintFrame(const QPoint &hoverPoint, int selectedIndex, const QRect &commitDirty)
{
    if (!m_renderer || !m_surface || !m_surface->canvas().isValid())
        return false;
    const BoxDef d = def();
    QImage &canvasImage = m_surface->canvas().image();
    // 画布尺寸/格式（分辨率或 DPI）变了才重新分配暂存帧，平时复用
    if (m_staging.size() != canvasImage.size() || m_staging.format() != canvasImage.format())
        m_staging = QImage(canvasImage.size(), canvasImage.format());
    // 尺寸纪律（T-060）：行数随条目自动增高（不超屏），超容量翻页；面板宽 =
    // 列数×格子宽恒为整数倍
    const BoxLayout::Sizing sz = BoxLayout::compute(QPoint(d.x, d.y), d.cols, m_items.size(),
                                                    m_surface->screenPhysical());
    const BoxLayout layout(sz.panel, d.cols, m_items.size(), d.page);
    ensureVisibleIcons(layout);
    QPainter g(&m_staging);
    m_layout = m_renderer->render(&g, d, layout, m_items, hoverPoint, selectedIndex, m_iconCache);
    g.end();
    // 悬停这类只动了格子/头部底色的高频小改动带脏矩形提交，免整屏合成发布
    if (commitDirty.isEmpty())
        m_surface->commitSurface();
    else
        m_surface->commitSurface(commitDirty);
    return true;
}

// 对当前页可见条目补发图标请求（contains 跳过已请求/负缓存/在飞）；顺带把面板
// 图标缓存裁剪到活条目（防只增不减）。副作用：请求完成异步回灌 m_iconCache 并
// 触发整帧重绘
void BoxPanel::ensureVisibleIcons(const BoxLayout &layout)
{
    // 可见区间口径与渲染器共用 layout 的 firstVisibleIndex/visibleCount（勿双写公式）
    const int first = layout.firstVisibleIndex();
    const int visible = layout.visibleCount(m_items.size());
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

// 鼠标离开面板：悬停目标归零（哨兵 kHoverOff）并按旧高亮脏矩形重绘清除；
// 无任何高亮可清则不动（m_hover 初值即哨兵，首次 leave 不空跑——T-062 修复 BUG-06）
void BoxPanel::onLeave()
{
    if (!eventsAccepted())
        return;
    if (m_hover != kHoverOff || m_lastHoverCell != -1 || m_lastHoverHeader != -1) {
        const QRect dirty = hoverVisualRect(m_lastHoverCell, m_lastHoverHeader);
        m_hover = kHoverOff;
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

// 单击路由：头部按钮（翻页/进调整）→ 页圆点跳页 → 格子单击仅选中（双击才启动）
// → 空白处清除选中；翻页/跳页走 render() 整帧提交，选中/清选走 renderFrame(脏矩形)
// 提交，进调整无面板提交（绘制由 overlay 调整模式接管）
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

// 盒头 ✛ 进调整模式：四个回调 = 拖动实时落盘+预览 / 确认写当前几何 / 取消恢复
// 进入前快照 / rectProvider 供 overlay 每帧回取面板矩形；overlay 关闭
//（WA_DeleteOnClose）时复位 m_adjusting。overlay 同时挂 m_overlay（QPointer）：
// 面板先亡时由析构掐断（T-062 修复 BUG-03）
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
    m_overlay = overlay; // T-062 修复: 供析构掐断（BUG-03）
    overlay->show();
    overlay->raise();
    overlay->activateWindow();
}

} // namespace poptools
