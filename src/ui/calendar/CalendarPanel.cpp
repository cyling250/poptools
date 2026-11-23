#include "CalendarPanel.h"

#include "CalendarEventEditor.h"
#include "CalendarSearchForm.h"
#include "core/hotkey/GlobalMouseHook.h"
#include "ui/shared/DesktopAdjustOverlay.h"
#include "ui/shared/DesktopScreens.h" // 多屏几何（归属屏解析/虚拟桌面并集）

#include <QGuiApplication>
#include <QScreen>

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>

namespace poptools {

CalendarPanel::CalendarPanel(QObject *parent)
    : QObject(parent)
{
    m_today = QDate::currentDate();
    m_year = m_today.year();
    m_month = m_today.month();
    // 重活（设置/日程 JSON、渲染器与壁纸资源）留在 onDutyStarted 懒建（红线 6）

    // 屏幕布局变化 → 比例映射自愈（T-076 用户实测 2026-11-22：外接屏拔掉后
    // 日历仍按旧几何显示，超出新屏被裁）。screenRemoved/primaryScreenChanged/
    // geometryChanged 三源连发，200ms 防抖合并成一次映射
    m_screenDebounce = new QTimer(this);
    m_screenDebounce->setSingleShot(true);
    m_screenDebounce->setInterval(200);
    connect(m_screenDebounce, &QTimer::timeout, this,
            &CalendarPanel::onScreenLayoutChanged);
    const auto kick = [this] { m_screenDebounce->start(); };
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, kick);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, kick);
    for (QScreen *s : QGuiApplication::screens())
        connect(s, &QScreen::geometryChanged, this, kick);
    connect(qGuiApp, &QGuiApplication::screenAdded, this,
            [this, kick](QScreen *s) {
                connect(s, &QScreen::geometryChanged, this, kick);
                kick();
            });
}

// 主屏 DPR 变化（T-077b）：renderer 重建（字号/间距随新 dpr）+ 日历视觉矩形
// 等比重映射——150%↔100% 切换时物理 rect 相同而视觉占屏比变了，按 dpr 比率
// 缩放使日历视觉大小跟随系统元素。壁纸缓存随旧 renderer 丢失 → 异步重算。
// 与 onScreenLayoutChanged 分层：本函数只管 dpr 比率，屏几何比率归那条链；
// 两链复合（拔屏常伴 dpr 切换）时线性无关、次序无关。
void CalendarPanel::onDprChanged()
{
    const qreal newDpr = m_surface->dpr();
    if (qFuzzyCompare(newDpr, m_lastDpr) || m_lastDpr <= 0.0)
        return;
    const double factor = double(newDpr) / m_lastDpr; // 视觉等比因子（先算后覆盖）
    m_lastDpr = newDpr;
    if (!m_renderer)
        return; // duty 未启动：renderer/设置懒建时按新 dpr 走，无需处理
    const QRect screen = m_surface->screenPhysical();
    m_renderer = std::make_unique<CalendarRenderer>(screen.width(), screen.height(),
                                                    newDpr);
    kickWallpaperRefresh(); // 壁纸缓存随旧 renderer 丢失
    // 视觉等比重映射：相对归属屏按 dpr 比率缩放（视觉占比不变）
    const QRect &s = m_screenNative;
    if (!s.isEmpty()) {
        m_settings.x = qRound(s.left() + (m_settings.x - s.left()) * factor);
        m_settings.y = qRound(s.top() + (m_settings.y - s.top()) * factor);
        m_settings.width = qRound(std::max(520.0, m_settings.width * factor));
        m_settings.height = qRound(std::max(420.0, m_settings.height * factor));
        clampSettings(); // 归属屏重解析 + 屏内钳制
        m_settings.save();
    }
    render();
}

// 拔屏/改分辨率/主屏切换：旧归属屏（m_screenNative）不再匹配当前布局时，
// 把日历矩形按新旧屏比例映射到新归属屏（中心仍在原归属屏则不动——无关屏
// 插拔与 DPI 微调不折腾面板；物理 rect 相同而 dpr 变的切换由 onDprChanged
// 处理，此处 target==oldScreen 直接返回不冲突）。画布=虚拟桌面并集且拔屏
// 只会变小，槽矩形缩回新并集即可，无需重建 SHM（T-063 架构红利）。
void CalendarPanel::onScreenLayoutChanged()
{
    const QRect oldScreen = m_screenNative;
    if (oldScreen.isEmpty())
        return;
    // 归属屏判定：中心点落在哪块屏；旧中心已不在任何屏（屏被拔）= 必须映射
    QRect target;
    for (const QRect &native : deskscreens::nativeScreenRects())
        if (native.contains(oldScreen.center()))
            target = native;
    if (!target.isEmpty() && target == oldScreen)
        return; // 归属屏健在且几何未变：不动（防抖余波/无关屏事件）
    if (target.isEmpty())
        target = deskscreens::nativeScreenRects().first(); // 主屏兜底（拔屏自愈）

    const QRect oldRect = m_settings.rect();
    const qint64 ow = std::max<qint64>(1, oldScreen.width());
    const qint64 oh = std::max<qint64>(1, oldScreen.height());
    m_settings.x = int(target.left()
                       + (oldRect.left() - oldScreen.left()) * target.width() / ow);
    m_settings.y = int(target.top()
                       + (oldRect.top() - oldScreen.top()) * target.height() / oh);
    m_settings.width = int(std::max<qint64>(520, oldRect.width() * target.width() / ow));
    m_settings.height = int(std::max<qint64>(420, oldRect.height() * target.height() / oh));
    clampSettings(); // 归属屏重解析 + 屏内钳制（m_screenNative 刷新）
    m_settings.save();
    kickWallpaperRefresh(); // 归属屏可能换了：毛玻璃壁纸按新屏重算（异步）
    render();
}

CalendarPanel::~CalendarPanel() = default;

QRect CalendarPanel::rectPhysical() const
{
    // 调整期间槽置空：桌面层日历消失（预览画在覆盖层，T-063），DLL 逐槽失效
    // 清掉旧位（远端每槽 prev 失效机制天然覆盖）
    if (m_hiddenDuringAdjust)
        return QRect();
    return m_layout.has_value() ? m_layout->panel : QRect();
}

QImage *CalendarPanel::frame()
{
    return m_staging.isNull() ? nullptr : &m_staging;
}

void CalendarPanel::onDutyStarted()
{
    if (!m_surface || !m_surface->canvas().isValid())
        return;
    if (!m_renderer) {
        const QRect screen = m_surface->screenPhysical();
        m_settings = CalendarSettings::load();
        m_store = CalendarTaskStore::load();
        clampSettings();
        m_renderer = std::make_unique<CalendarRenderer>(screen.width(), screen.height(),
                                                        m_surface->dpr());
        m_lastDpr = m_surface->dpr();
        // 主屏 DPR 变化（T-077b）：重建渲染器（字号/间距随新 dpr）+ 视觉矩形
        // 等比重映射——物理 rect 相同的屏切换（150%↔100%）也不漏
        connect(m_surface, &DesktopSurfaceHost::dprChanged, this,
                [this](qreal) { onDprChanged(); });
    }
    if (!m_clock) {
        m_clock = new QTimer(this);
        m_clock->setInterval(1000);
        connect(m_clock, &QTimer::timeout, this, &CalendarPanel::tickClock);
    }
    m_clock->start();
    if (!m_wallpaperTimer) {
        m_wallpaperTimer = new QTimer(this);
        m_wallpaperTimer->setInterval(30000);
        connect(m_wallpaperTimer, &QTimer::timeout, this,
                [this] { kickWallpaperRefresh(); });
    }
    m_wallpaperTimer->start();
    kickWallpaperRefresh(); // duty 启动即踢一次（冷启动首帧异步取壁纸，
                            // 渲染取图只回缓存，不再同步重算——T-071）
}

// 壁纸重算异步踢发（T-068 定时器体抽方法 + T-071 duty 启动/设置重载共用）：
// 重算（磁盘整图解码 + 两遍盒模糊，100~200ms 级）全链离主线程——主线程挂着
// WH_MOUSE_LL/WH_KEYBOARD_LL 低级钩子，主线程停摆=全系统鼠标排队；签名未变时
// refreshNeeded 内部已续时基免算，只有真换壁纸才进线程池
void CalendarPanel::kickWallpaperRefresh()
{
    if (!m_renderer || m_wallpaperBusy)
        return;
    if (!m_renderer->wallpaperRefreshNeeded())
        return;
    m_wallpaperBusy = true;
    const QSize sz = m_renderer->wallpaperSize();
    QPointer<CalendarPanel> self(this);
    QThreadPool::globalInstance()->start([self, sz] {
        const QImage img = DesktopWallpaperProvider::computeBlurred(sz.width(), sz.height());
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, img] {
            if (self)
                self->onWallpaperComputed(img);
        }, Qt::QueuedConnection);
    });
}

// 线程池壁纸成品回主线程：装缓存并整帧重绘（30s 定时器异步路径的尾段）
void CalendarPanel::onWallpaperComputed(const QImage &img)
{
    m_wallpaperBusy = false;
    if (img.isNull() || !m_renderer)
        return;
    m_renderer->adoptWallpaper(img);
    render();
}

void CalendarPanel::onDutyStopped()
{
    if (m_clock)
        m_clock->stop();
    if (m_wallpaperTimer)
        m_wallpaperTimer->stop();
}

void CalendarPanel::clampSettings()
{
    // 单屏归属（T-063 移植）：矩形中心落在哪块屏就归哪块屏，并钳制在该屏内
    //（跨屏拖动由调整覆盖层完成；拔屏时矩形被收回最近的可用屏）
    m_screenNative = deskscreens::resolveScreenForRect(m_settings.rect());
    if (m_screenNative.isEmpty())
        return;
    // dpr 比率精确换算（T-077b 终版）：rect 记录的是 SavedDpr 时代的物理
    // 坐标，主屏缩放切换（两次运行之间发生，运行时信号收不到）后按
    // current/saved 比率换算 → 逻辑尺寸恒定 = 视觉布局恒定。SavedDpr=0
    // （旧档）按「右/下缘超屏物理边界」启发式归一化兜底一次。
    QScreen *primary = QGuiApplication::primaryScreen();
    const qreal curDpr = primary ? primary->devicePixelRatio() : 1.0;
    const QRect &s = m_screenNative;
    if (m_settings.savedDpr > 0.0 && !qFuzzyCompare(m_settings.savedDpr, curDpr)) {
        const qreal f = curDpr / m_settings.savedDpr;
        m_settings.x = s.left() + qRound((m_settings.x - s.left()) * f);
        m_settings.y = s.top() + qRound((m_settings.y - s.top()) * f);
        m_settings.width = qRound(m_settings.width * f);
        m_settings.height = qRound(m_settings.height * f);
        qInfo("CalendarPanel: dpr %.2f→%.2f，rect 按 %.3f 等比换算",
              m_settings.savedDpr, curDpr, f);
        m_settings.savedDpr = curDpr;
        m_settings.save();
    } else if (m_settings.savedDpr <= 0.0) {
        const int rectRight = m_settings.x + m_settings.width;
        const int rectBottom = m_settings.y + m_settings.height;
        if (rectRight > s.right() + 1 || rectBottom > s.bottom() + 1) {
            // 旧档且超屏：另一缩放配置下记的坐标 → 归一化「右/下缘恰在屏缘」
            const qreal f = qMin(qreal(s.width()) / qMax(1, rectRight - s.left()),
                                 qreal(s.height()) / qMax(1, rectBottom - s.top()));
            m_settings.x = s.left() + qRound((m_settings.x - s.left()) * f);
            m_settings.y = s.top() + qRound((m_settings.y - s.top()) * f);
            m_settings.width = qRound(m_settings.width * f);
            m_settings.height = qRound(m_settings.height * f);
            qInfo("CalendarPanel: 旧档 rect 超出归属屏，按 %.2f 归一化回缩", f);
        }
        m_settings.savedDpr = curDpr; // 本轮起有基准
        m_settings.save();
    }
    const int w = std::clamp(m_settings.width, 520, std::max(520, s.width()));
    const int h = std::clamp(m_settings.height, 420, std::max(420, s.height()));
    const int x = std::clamp(m_settings.x, s.left(), std::max(s.left(), s.right() + 1 - w));
    const int y = std::clamp(m_settings.y, s.top(), std::max(s.top(), s.bottom() + 1 - h));
    m_settings.x = x;
    m_settings.y = y;
    m_settings.width = w;
    m_settings.height = h;
}

void CalendarPanel::showCalendar()
{
    m_settings.hidden = false;
    m_settings.save();
    if (m_surface)
        m_surface->resumeDuty(); // 幂等；已值守时 onDutyStarted 不会重复
    render();
}

void CalendarPanel::reloadSettings()
{
    // 应用设置页保存日历设置后的入口：重读 JSON、应用几何钳制、重绘
    m_settings = CalendarSettings::load();
    clampSettings();
    kickWallpaperRefresh(); // T-071：同步 refresh 已删，重载走异步踢发
    render();
}

void CalendarPanel::render(bool force)
{
    // force：确认回调里立即恢复桌面帧（跳过 m_adjusting 守卫，无空窗）
    if (!force && m_adjusting)
        return;
    renderFrame(m_hover, m_hoverHint, false);
}

// ---- Goody 工作摘要（T-067） ----

// 注入查询槽即强制刷新（CalendarModule init 时 Goody 槽位已就绪）
void CalendarPanel::setDigestQuery(std::function<QString(int, int, int)> daily,
                                   std::function<QString(int, int)> monthly)
{
    m_digestQuery = std::move(daily);
    m_monthlyQuery = std::move(monthly);
    refreshSummaries(true);
}

// 重建显示月摘要缓存（同月且非强制直接跳过——render 热路径去重）；查询为
// 逐日文件存在性 stat（≤31 次，月/信号驱动频率，非热路径）
void CalendarPanel::refreshSummaries(bool force)
{
    const QString key = QStringLiteral("%1-%2")
                            .arg(m_year)
                            .arg(m_month, 2, 10, QLatin1Char('0'));
    if (!force && m_digestViewMonth == key)
        return;
    m_digestViewMonth = key;
    m_digestView = CalendarDigestView();
    if (m_digestQuery) {
        const QDate first(m_year, m_month, 1);
        const int days = first.daysInMonth();
        for (int d = 1; d <= days; ++d) {
            const QString p = m_digestQuery(m_year, m_month, d);
            if (!p.isEmpty())
                m_digestView.daily.insert(QDate(m_year, m_month, d), p);
        }
    }
    if (m_monthlyQuery)
        m_digestView.monthly = m_monthlyQuery(m_year, m_month);
    render();
}

// 摘要 notepad 弹出逻辑（md→%TEMP% txt→startDetached）2026-11-22 提升为
// ui/calendar 域共享自由函数 openDigestInNotepad（CalendarEventEditor.h）：
// 月报链接（本文件）与日报链接（编辑器标题行，用户拍板改道）共用。

// S-04：render()/renderDuringAdjust() 的公共体（原两函数除 m_adjusting 守卫与
// hover/hint 实参外逐行相同）。先画进私有暂存帧再整帧发布：直接在共享位图上
// "清屏→重绘"的 10~30ms 窗口内，explorer 的擦除流程可能采到近全透明的中间态
// ——悬停头部按钮时连续两次渲染重叠，表现为日历整面板消失一帧再恢复（闪烁，T-058）
void CalendarPanel::renderFrame(const QPoint &hover, const QString &hoverHint,
                                bool suppressIfAdjusting, const QRect &commitDirty)
{
    if (suppressIfAdjusting && m_adjusting)
        return;
    if (!m_renderer || !m_surface || !m_surface->canvas().isValid())
        return;
    refreshSummaries(false); // 跨月自动重建缓存（同月键直接跳过，无开销）
    QImage &canvasImage = m_surface->canvas().image();
    if (m_staging.size() != canvasImage.size() || m_staging.format() != canvasImage.format())
        m_staging = QImage(canvasImage.size(), canvasImage.format());
    QPainter g(&m_staging);
    m_layout = m_renderer->render(&g, m_surface->canvas(), m_settings, m_store, m_today, m_year,
                                  m_month, hover, hoverHint, false, m_digestView);
    g.end();
    // 暂存帧恒整帧绘制；悬停这类只动头部底色/链接高亮的小改动带脏矩形提交，
    // 免整屏（虚拟桌面并集画布）合成发布（T-071，BoxPanel 同款）
    if (commitDirty.isEmpty())
        m_surface->commitSurface();
    else
        m_surface->commitSurface(commitDirty);
}

QRect CalendarPanel::hoverVisualRect(int headerIdx, const QString &hint, bool monthHov) const
{
    QRect r;
    if (!m_layout.has_value())
        return r;
    if (headerIdx >= 0 && headerIdx < m_layout->headerButtons.size())
        r = r.united(m_layout->headerButtons.at(headerIdx));
    if (!hint.isEmpty())
        r = r.united(m_layout->guidePill);
    if (monthHov)
        r = r.united(m_layout->monthLink);
    return r;
}

void CalendarPanel::tickClock()
{
    const QDate now = QDate::currentDate();
    if (now == m_today)
        return;
    const bool wasCurrent = m_year == m_today.year() && m_month == m_today.month();
    m_today = now;
    if (wasCurrent) {
        m_year = now.year();
        m_month = now.month();
    }
    render();
}

QPoint CalendarPanel::logicalPos(const QPoint &physical) const
{
    const qreal dpr = m_surface ? m_surface->dpr() : 1.0;
    if (dpr <= 0.0)
        return physical;
    return QPoint(qRound(physical.x() / dpr), qRound(physical.y() / dpr));
}

void CalendarPanel::onMove(const QPoint &p)
{
    if (!eventsAccepted())
        return;
    updateHover(p);
}

void CalendarPanel::onLeave()
{
    if (!eventsAccepted())
        return;
    if (m_hover != QPoint(-9999, -9999) || !m_hoverHint.isEmpty()) {
        const QRect dirty = hoverVisualRect(m_lastHeaderHover, m_hoverHint,
                                            m_lastMonthLinkHov);
        m_hover = QPoint(-9999, -9999);
        m_hoverHint.clear();
        m_lastHeaderHover = -1;
        m_lastMonthLinkHov = false; // 离开面板全量复位（原漏复位两个链接标记：
        renderFrame(m_hover, m_hoverHint, true, dirty.adjusted(-2, -2, 2, 2));
    }
}

void CalendarPanel::onClick(const QPoint &p)
{
    // 摘要格内链接已撤（2026-11-22 改道编辑器标题行）；编辑器开启期间其余
    // 事件维持模态抑制
    if (!eventsAccepted())
        return;
    handleSingleClick(p);
}

void CalendarPanel::onDoubleClick(const QPoint &p)
{
    if (!eventsAccepted())
        return;
    handleDoubleClick(p);
}

void CalendarPanel::updateHover(const QPoint &p)
{
    if (!m_layout.has_value()) {
        render();
        if (!m_layout.has_value())
            return;
    }
    m_hover = p;
    // 头部按钮只做悬停底色，不弹文字气泡（气泡压住星期表头，被用户否掉）；
    // 仅首次使用引导气泡保留。月报链接悬停走"变化才重绘"
    QString hint;
    const int hh = m_layout->hitHeader(p);
    const bool monthHov = !m_digestView.monthly.isEmpty() && m_layout->monthLink.contains(p);
    if (hh < 0 && !monthHov && m_settings.showGuide && m_layout->guidePill.contains(p)) {
        hint = QStringLiteral("调整日历清单的位置和大小");
    }
    if (hh != m_lastHeaderHover || hint != m_hoverHint || monthHov != m_lastMonthLinkHov) {
        // T-071：悬停级小改动带脏矩形提交（新旧语义目标可视矩形并集，外扩
        // 2px 兜描边/悬停底外扩）；翻页/跨日等大改动仍走 render() 整帧
        const QRect dirty = hoverVisualRect(m_lastHeaderHover, m_hoverHint,
                                            m_lastMonthLinkHov)
                                .united(hoverVisualRect(hh, hint, monthHov));
        m_lastHeaderHover = hh;
        m_hoverHint = hint;
        m_lastMonthLinkHov = monthHov;
        renderFrame(p, hint, true, dirty.adjusted(-2, -2, 2, 2));
    }
}

void CalendarPanel::handleSingleClick(const QPoint &p)
{
    if (!m_layout.has_value())
        return;
    const int hh = m_layout->hitHeader(p);
    if (hh >= 0) {
        // T-062 修复: G-03 边界翻页跳变——原实现先做月加减再 clamp 年份，
        // 1901-01 再往前会跳到 1901-12、2100-12 往后跳到 2100-01（跳变 11 个月），
        // clamp 本意应是"停在边界"。翻页前先守卫，clamp 保留作兜底
        switch (CalendarLayout::headerKinds[hh]) {
            case HeaderButtonKind::Previous:
                // 下限 1901 而非 1900：农历表自 1900-01-31（正月初一）起，
                // 1900 年首月翻页会让格首落入表前无数据区，故从 1901-01 起可翻（L-07）
                if (m_year == 1901 && m_month == 1)
                    break;
                m_month--;
                while (m_month < 1) {
                    m_month += 12;
                    m_year--;
                }
                m_year = std::clamp(m_year, 1901, 2100);
                render();
                break;
            case HeaderButtonKind::Next:
                if (m_year == 2100 && m_month == 12)
                    break;
                m_month++;
                while (m_month > 12) {
                    m_month -= 12;
                    m_year++;
                }
                m_year = std::clamp(m_year, 1901, 2100);
                render();
                break;
            case HeaderButtonKind::Adjust:
                enterAdjustMode();
                break;
        }
        return;
    }
    if (m_settings.showGuide && m_layout->guidePill.contains(p)) {
        m_settings.showGuide = false;
        m_settings.save();
        render();
        return;
    }
    // 月报链接（头部左上角，T-067）：有月报才可点，点击 notepad 弹 txt
    if (!m_digestView.monthly.isEmpty() && m_layout->monthLink.contains(p)) {
        openDigestInNotepad(m_digestView.monthly);
        return;
    }
    QDate date;
    QRect cell;
    int row = 0, col = 0;
    // T-062 修复: G-05 单击格子的唯一效果是消引导气泡——原实现无 showGuide
    // 前置条件，引导消失后每次点击仍同步覆写 settings.json + 一次无差异整帧
    // 渲染（写盘 churn）。与上方胶囊分支同款条件式写法；设置键格式不变
    if (m_settings.showGuide && m_layout->tryGetCell(p, &row, &col, &date, &cell)
        && m_layout->inDisplayMonth(date)) {
        m_settings.showGuide = false;
        m_settings.save();
        render();
    }
}

void CalendarPanel::handleDoubleClick(const QPoint &p)
{
    if (!m_layout.has_value())
        return;
    QDate date;
    QRect cell;
    int row = 0, col = 0;
    if (!m_layout->tryGetCell(p, &row, &col, &date, &cell))
        return;
    if (!m_layout->inDisplayMonth(date))
        return;
    openEditor(date, cell);
}

void CalendarPanel::openEditor(const QDate &date, const QRect &cell)
{
    // 坐标口径（T-062 修复: G-02）：cell/screen/编辑器落点均为物理像素，而
    // 编辑器尺寸（kEditorSize）是 Qt 逻辑坐标（CalendarEventEditor 构造
    // resize）。w/h 先按 dpr 折算成物理尺寸参与定位与钳制，最终落点经
    // logicalPos 转回逻辑坐标传给编辑器（Qt 窗口 move 用逻辑坐标）
    const int w = CalendarEventEditor::kEditorSize.width(); // 逻辑尺寸（S-07 收敛常量）
    const int h = CalendarEventEditor::kEditorSize.height();
    const qreal dpr = m_surface ? m_surface->dpr() : 1.0;
    const int wPhys = qRound(w * (dpr > 0.0 ? dpr : 1.0));
    const int hPhys = qRound(h * (dpr > 0.0 ? dpr : 1.0));
    const QRect screen = m_surface ? m_surface->screenPhysical() : QRect();
    // T-072（用户拍板）：编辑器对齐所点击日期格居中——原实现钉在格子左上角
    // (cell.left+12, cell.top+12)，视觉偏离所点日期。编辑器远大于格子，以格
    // 心为锚居中后再按屏幕钳制（右缘 12px/底缘 60px 余量、左/顶 10px 下限）
    int x = cell.center().x() - wPhys / 2;
    int y = cell.center().y() - hPhys / 2;
    if (x + wPhys > screen.right() + 1 - 12)
        x = screen.right() + 1 - 12 - wPhys;
    if (y + hPhys > screen.bottom() + 1 - 60) // 60 = 底部预留（任务栏高度余量）
        y = screen.bottom() + 1 - hPhys - 60;
    x = std::max(screen.left() + 10, x);
    y = std::max(screen.top() + 10, y);
    // 摘要入口（2026-11-22 用户拍板）：当日有日报则编辑器标题行显示「摘要»」
    // 链接，点击直接 notepad（格内链接已撤，不再有"双击开面板后才可点"链路）
    auto *editor = new CalendarEventEditor(date, &m_store, logicalPos(QPoint(x, y)),
                                           m_digestView.daily.value(date), nullptr);
    trackModal(editor); // m_modal/m_modalWidget 唯一权威置位处（S-08 删去开头重复裸置位）
    // 外点关闭兜底：编辑器靠 WindowDeactivate 关闭，但双击源自桌面（前台进程是
    // explorer），activateWindow 作为后台进程常被 Windows 前台锁拒绝——编辑器
    // 从未激活过，外点就永远不会"失焦"，表现即"有时点外面关不掉、且无规律"
    //（实为激活是否成功无规律）。共享 WH_MOUSE_LL 按需装卸（划词卡片先例，
    // 红线 2）：点编辑器外即保存关闭，编辑器销毁即卸钩
    if (!m_editorHook) {
        m_editorHook = new GlobalMouseHook(this);
        connect(m_editorHook, &GlobalMouseHook::mousePressed, this,
                [this](const QPoint &nativePos) {
                    auto *ed = qobject_cast<CalendarEventEditor *>(m_modalWidget.data());
                    if (!ed)
                        return;
                    if (!ed->frameGeometry().contains(logicalPos(nativePos)))
                        ed->saveAndClose();
                });
    }
    m_editorHook->start();
    connect(editor, &QObject::destroyed, this, [this] {
        if (m_editorHook)
            m_editorHook->stop();
    });
    editor->show();
    editor->raise();
    editor->activateWindow();
}

void CalendarPanel::centerOnPanel(QWidget *w)
{
    // 弹窗锚定日历面板中心（无 parent 的 QDialog 默认落在"活动屏"，多屏机器上
    // 曾开到副屏导致用户以为日历无响应）；物理 → 逻辑换算见坐标约定
    if (m_surface)
        m_surface->centerOnPanelRect(w, m_settings.rect());
}

void CalendarPanel::enterAdjustMode()
{
    if (m_adjusting || !m_surface)
        return;
    m_adjusting = true;
    m_settings.showGuide = false;
    m_previewMs = 0;
    m_previewPending = false;
    const QRect original = m_settings.rect();
    // 调整期间桌面层日历隐藏（槽置空），预览画在覆盖层里——跟虚线框同帧同层，
    // 跨进程擦除链路退出拖动感知路径（T-063 跟手根治）
    m_hiddenDuringAdjust = true;
    m_surface->commitSurface();
    updateAdjustPreview(true);
    // 拖动范围 = 整个虚拟桌面：矩形中心跨过屏幕边界即整体换屏
    const QList<QRect> screenRects = deskscreens::nativeScreenRects();
    QRect desktopPhysical;
    QRect desktopLogical;
    const auto screens = QGuiApplication::screens();
    for (QScreen *s : screens)
        desktopLogical = desktopLogical.united(s->geometry());
    desktopPhysical = deskscreens::virtualDesktopPhysical();
    if (desktopPhysical.isEmpty())
        desktopPhysical = m_screenNative;
    if (desktopLogical.isEmpty())
        desktopLogical = QRect(0, 0, m_screenNative.width(), m_screenNative.height());
    const qreal dpr = m_surface->dpr();
    auto *overlay =
        new DesktopAdjustOverlay(original, desktopPhysical, desktopLogical, screenRects,
                                 dpr, [this] { return m_adjustPreview; },
                                 [this](const QRect &r) {
                                     m_settings.x = r.x();
                                     m_settings.y = r.y();
                                     m_settings.width = r.width();
                                     m_settings.height = r.height();
                                     clampSettings();
                                     // 画布=虚拟桌面并集，跨屏无需重建；仅归属屏
                                     // 元数据随 clampSettings 更新
                                     updateAdjustPreview(false); // 尺寸变化才重渲染
                                 },
                                 [this](const QRect &r) {
                                     m_settings.x = r.x();
                                     m_settings.y = r.y();
                                     m_settings.width = r.width();
                                     m_settings.height = r.height();
                                     clampSettings();
                                     m_settings.save();
                                     // 立即恢复桌面帧（槽恢复 + 合成发布），无空窗
                                     m_hiddenDuringAdjust = false;
                                     render(true);
                                     m_surface->requestFullRepaint(); // 全失效兜底
                                 },
                                 nullptr);
    connect(overlay, &QObject::destroyed, this, [this] {
        m_adjusting = false;
        m_hiddenDuringAdjust = false;
        m_adjustPreview = QImage();
        render(); // 确认路径已渲染过；取消路径（Esc/✓ 之外）由此恢复桌面帧
    });
    overlay->show();
    overlay->raise();
    overlay->activateWindow();
}

// 调整预览帧渲染：面板内容 + 四周 32px 余量（阴影/发光）；移动模式尺寸不变
// → 复用既有帧（overlay 平移绘制，宿主零参与 = 跟手天花板）；缩放模式尺寸
// 变化才节流重渲染（16ms + 补帧定时器兜底）
void CalendarPanel::updateAdjustPreview(bool force)
{
    if (!m_renderer)
        return;
    constexpr int kPreviewMargin = 32;
    const QSize want = QSize(m_settings.width, m_settings.height) + QSize(64, 64);
    if (want != m_adjustPreview.size())
        m_adjustPreview = QImage(want, QImage::Format_ARGB32_Premultiplied);
    else if (!force)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 since = now - m_previewMs;
    if (!force && m_previewMs > 0 && since >= 0 && since < 16) {
        if (!m_previewPending) {
            m_previewPending = true;
            QTimer::singleShot(int(16 - since), this, [this] {
                m_previewPending = false;
                if (m_adjusting)
                    updateAdjustPreview(true);
            });
        }
        return;
    }
    QPainter p(&m_adjustPreview);
    // 面板画在预览帧 (32,32)；renderer 会污染头部日历矩形为预览坐标，画完
    // 恢复真实虚拟坐标（桌面层隐藏期间头部仅脏矩形用途，保持一致）
    CalendarSettings local = m_settings;
    local.x = kPreviewMargin;
    local.y = kPreviewMargin;
    m_renderer->render(&p, m_surface->canvas(), local, m_store, m_today, m_year, m_month,
                       QPoint(-9999, -9999), QString(), false);
    p.end();
    m_previewMs = QDateTime::currentMSecsSinceEpoch();
}

void CalendarPanel::searchTasks()
{
    auto *form = new CalendarSearchForm(m_store, nullptr);
    connect(form, &QDialog::finished, this, [this, form](int result) {
        if (result == QDialog::Accepted) {
            const QDate d = form->selectedDate();
            if (d.isValid()) {
                m_year = d.year();
                m_month = d.month();
                render();
                // T-062 修复: G-06 值守未启（stopDuty 后或尚未 startDuty）时
                // render() 是 no-op，m_layout 仍是上一次的旧月布局——以旧
                // firstCellDate 换算 row/col 会把编辑器开在错误格子的矩形上
                //（日期语义正确、位置错）。布局与目标月一致才开编辑器，否则
                // 只翻月；m_layout 为空（从未渲染过）同样跳过
                if (m_layout.has_value() && m_layout->year == d.year()
                    && m_layout->month == d.month()) {
                    const int row = int(QDate(m_layout->firstCellDate).daysTo(d) / 7);
                    const int col = int(QDate(m_layout->firstCellDate).daysTo(d) % 7);
                    if (row >= 0 && row < m_layout->rows && col >= 0 && col < 7)
                        openEditor(d, m_layout->cellRect(row, col));
                }
            }
        }
        form->deleteLater();
    });
    trackModal(form);
    centerOnPanel(form);
    form->show();
    form->raise();
    form->activateWindow();
}

void CalendarPanel::pickDate()
{
    auto *form = new CalendarDatePickerForm(m_today, nullptr);
    connect(form, &QDialog::finished, this, [this, form](int result) {
        if (result == QDialog::Accepted) {
            const QDate d = form->selectedDate();
            if (d.isValid()) {
                m_year = d.year();
                m_month = d.month();
                render();
            }
        }
        form->deleteLater();
    });
    trackModal(form);
    centerOnPanel(form);
    form->show();
    form->raise();
    form->activateWindow();
}

void CalendarPanel::exportTasks()
{
    const QString file = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("导出"), QStringLiteral("calendar-tasks.json"),
        // T-062 修复: G-04 过滤器语法——"描述|*.json" 不是 Qt 语法（应为
        // "描述 (*.ext)"），原串被当作无通配模式的一条过滤器，文件列表不按
        // 扩展名过滤
        QStringLiteral("JSON 文件 (*.json)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(m_store.exportJson().toUtf8());
        showInfo(QStringLiteral("导出"), QStringLiteral("已导出到：\n") + file);
    } else {
        showInfo(QStringLiteral("导出失败"), f.errorString());
    }
}

void CalendarPanel::importTasks()
{
    const QString file =
        QFileDialog::getOpenFileName(nullptr, QStringLiteral("导入"), QString(),
                                     QStringLiteral("JSON 文件 (*.json)")); // T-062 修复: G-04
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        showInfo(QStringLiteral("导入失败"), f.errorString());
        return;
    }
    CalendarTaskStore imported;
    if (imported.fromJson(QString::fromUtf8(f.readAll()))) {
        m_store.mergeFrom(imported);
        render();
        showInfo(QStringLiteral("导入"), QStringLiteral("导入完成。"));
    } else {
        showInfo(QStringLiteral("导入失败"), QStringLiteral("JSON 解析失败。"));
    }
}

void CalendarPanel::showInfo(const QString &title, const QString &text)
{
    QMessageBox *box = new QMessageBox(QMessageBox::Information, title, text,
                                       QMessageBox::Ok, nullptr);
    connect(box, &QDialog::finished, this, [box](int) { box->deleteLater(); });
    trackModal(box);
    box->show();
    centerOnPanel(box); // show 后尺寸才定，随即搬回日历所在屏
    box->raise();
    box->activateWindow();
}

void CalendarPanel::trackModal(QWidget *w)
{
    // 弹窗登记：m_modal=true 直到该窗真正销毁。用 QPointer 对号防串——
    // 菜单点菜单项时「close(异步销毁) → action 开新弹窗」存在竞态，
    // 旧菜单的 destroyed 若无条件复位会把新弹窗的模态态误清
    m_modal = true;
    m_modalWidget = w;
    connect(w, &QObject::destroyed, this, [this, w] {
        if (m_modalWidget == w) {
            m_modalWidget = nullptr;
            m_modal = false;
            render();
        }
    });
}

// T-062 修复: G-10 增补 activeModalWidget 兜底：exportTasks/importTasks 的
// QFileDialog 静态函数自带模态循环、不经 trackModal（m_modal 不置位），对话框
// 打开期间 explorer 转发的桌面鼠标事件仍被处理（悬停重绘、点击翻月，用户关闭
// 对话框后发现月份被误翻）。该判断覆盖现有与未来一切原生模态；openEditor 的
// 无边框编辑器不是 activeModalWidget，仍由 m_modal 管控
bool CalendarPanel::eventsAccepted() const
{
    return !m_modal && !m_adjusting && QApplication::activeModalWidget() == nullptr;
}

} // namespace poptools
