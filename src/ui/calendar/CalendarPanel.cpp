#include "CalendarPanel.h"

#include "CalendarEventEditor.h"
#include "CalendarSearchForm.h"
#include "core/hotkey/GlobalMouseHook.h"
#include "ui/shared/DesktopAdjustOverlay.h"

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPainter>
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
}

CalendarPanel::~CalendarPanel() = default;

QRect CalendarPanel::rectPhysical() const
{
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
        connect(m_wallpaperTimer, &QTimer::timeout, this, [this] {
            if (!m_renderer)
                return;
            m_renderer->refreshWallpaper();
            render();
        });
    }
    m_wallpaperTimer->start();
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
    const QRect screen = m_surface ? m_surface->screenPhysical() : QRect();
    if (screen.isEmpty())
        return;
    const int w = std::clamp(m_settings.width, 520, std::max(520, screen.width()));
    const int h = std::clamp(m_settings.height, 420, std::max(420, screen.height()));
    const int x = std::clamp(m_settings.x, screen.left(),
                             std::max(screen.left(), screen.right() + 1 - w));
    const int y = std::clamp(m_settings.y, screen.top(),
                             std::max(screen.top(), screen.bottom() + 1 - h));
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
    if (m_renderer)
        m_renderer->refreshWallpaper();
    render();
}

void CalendarPanel::render()
{
    if (m_adjusting || !m_renderer || !m_surface || !m_surface->canvas().isValid())
        return;
    // 先画进私有暂存帧再整帧发布：直接在共享位图上"清屏→重绘"的 10~30ms
    // 窗口内，explorer 的擦除流程可能采到近全透明的中间态——悬停头部按钮
    // 时连续两次渲染重叠，表现为日历整面板消失一帧再恢复（闪烁，T-058）
    QImage &canvasImage = m_surface->canvas().image();
    if (m_staging.size() != canvasImage.size() || m_staging.format() != canvasImage.format())
        m_staging = QImage(canvasImage.size(), canvasImage.format());
    QPainter g(&m_staging);
    m_layout = m_renderer->render(&g, m_surface->canvas(), m_settings, m_store, m_today, m_year,
                                  m_month, m_hover, m_hoverHint, false);
    g.end();
    m_surface->commitSurface();
}

void CalendarPanel::renderDuringAdjust()
{
    if (!m_renderer || !m_surface || !m_surface->canvas().isValid())
        return;
    QImage &canvasImage = m_surface->canvas().image();
    if (m_staging.size() != canvasImage.size() || m_staging.format() != canvasImage.format())
        m_staging = QImage(canvasImage.size(), canvasImage.format());
    QPainter g(&m_staging);
    // 调整装饰（虚线框/✓×）由覆盖层窗口自绘，位图只画正常内容（与原实现一致）
    m_layout = m_renderer->render(&g, m_surface->canvas(), m_settings, m_store, m_today, m_year,
                                  m_month, QPoint(-9999, -9999), QString(), false);
    g.end();
    m_surface->commitSurface();
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
        m_hover = QPoint(-9999, -9999);
        m_hoverHint.clear();
        m_lastHeaderHover = -1;
        render();
    }
}

void CalendarPanel::onClick(const QPoint &p)
{
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
    // 仅首次使用引导气泡保留
    QString hint;
    const int hh = m_layout->hitHeader(p);
    if (hh < 0 && m_settings.showGuide && m_layout->guidePill.contains(p)) {
        hint = QStringLiteral("调整日历清单的位置和大小");
    }
    if (hh != m_lastHeaderHover || hint != m_hoverHint) {
        m_lastHeaderHover = hh;
        m_hoverHint = hint;
        render();
    }
}

void CalendarPanel::handleSingleClick(const QPoint &p)
{
    if (!m_layout.has_value())
        return;
    const int hh = m_layout->hitHeader(p);
    if (hh >= 0) {
        switch (CalendarLayout::headerKinds[hh]) {
            case HeaderButtonKind::Previous:
                m_month--;
                while (m_month < 1) {
                    m_month += 12;
                    m_year--;
                }
                m_year = std::clamp(m_year, 1901, 2100);
                render();
                break;
            case HeaderButtonKind::Next:
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
    QDate date;
    QRect cell;
    int row = 0, col = 0;
    if (m_layout->tryGetCell(p, &row, &col, &date, &cell)
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
    m_modal = true;
    const int w = 380, h = 430;
    const QRect screen = m_surface ? m_surface->screenPhysical() : QRect();
    int x = cell.left() + 12;
    if (x + w > screen.right() + 1 - 12)
        x = cell.left() + cell.width() - w - 12;
    int y = cell.top() + 12;
    if (y + h > screen.bottom() + 1 - 60)
        y = screen.bottom() + 1 - h - 60;
    x = std::max(screen.left() + 10, x);
    y = std::max(screen.top() + 10, y);
    auto *editor = new CalendarEventEditor(date, &m_store, logicalPos(QPoint(x, y)), nullptr);
    trackModal(editor);
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
                    if (ed && !ed->frameGeometry().contains(logicalPos(nativePos)))
                        ed->saveAndClose();
                });
    }
    m_editorHook->start();
    connect(editor, &QObject::destroyed, this,
            [this] { if (m_editorHook) m_editorHook->stop(); });
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
    const QRect original = m_settings.rect();
    auto *overlay =
        new DesktopAdjustOverlay(original, m_surface->screenPhysical(), m_surface->dpr(),
                                 [this](const QRect &r) {
                                     m_settings.x = r.x();
                                     m_settings.y = r.y();
                                     m_settings.width = r.width();
                                     m_settings.height = r.height();
                                     renderDuringAdjust();
                                 },
                                 [this](const QRect &r) {
                                     m_settings.x = r.x();
                                     m_settings.y = r.y();
                                     m_settings.width = r.width();
                                     m_settings.height = r.height();
                                     m_settings.save();
                                 },
                                 [this, original]() {
                                     m_settings.x = original.x();
                                     m_settings.y = original.y();
                                     m_settings.width = original.width();
                                     m_settings.height = original.height();
                                     m_settings.save();
                                 },
                                 nullptr);
    connect(overlay, &QObject::destroyed, this, [this, original] {
        m_adjusting = false;
        // 取消路径：overlay 内部已回调 onCanceled 恢复 original 并 save；
        // 关闭后无论哪条路径都回到正常渲染
        render();
    });
    overlay->show();
    overlay->raise();
    overlay->activateWindow();
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
                if (m_layout.has_value()) {
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
        QStringLiteral("JSON 文件|*.json"));
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
                                     QStringLiteral("JSON 文件|*.json"));
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

} // namespace poptools
