#include "CalendarHostWidget.h"

#include "CalendarAdjustOverlay.h"
#include "CalendarEventEditor.h"
#include "CalendarMenuForm.h"
#include "CalendarSearchForm.h"
#include "CalendarSettingsForm.h"

#include "core/hotkey/GlobalMouseHook.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QLibrary>
#include <QMessageBox>
#include <QPainter>
#include <QScreen>
#include <QTimer>

#include <algorithm>

namespace poptools {

namespace {
// 与注入 DLL 的协议常量一致
constexpr UINT kMsgUpdate = WM_APP + 0x101;
constexpr UINT kMsgEvent = WM_APP + 0x103;
constexpr int kEvMove = 1;
constexpr int kEvDown = 2;
constexpr int kEvUp = 3;
constexpr int kEvDblclk = 4;
constexpr int kEvLeave = 5;

// 与 DLL 约定的框架窗口标识（NotifyHookUpdate 用）
HWND findHookFrameworkWindow()
{
    return ::FindWindowW(L"PopToolsCalendarHook_Framework", L"PopToolsCalendarHook_Title");
}
} // namespace

CalendarHostWidget::CalendarHostWidget(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
{
    setObjectName("CalendarHostWidget");
    setAttribute(Qt::WA_ShowWithoutActivating);
    // 隐藏宿主窗口：1x1 放屏幕外，仅接收 DLL 转发的鼠标事件消息
    setGeometry(-32000, -32000, 1, 1);
    show();

    m_screen = QGuiApplication::primaryScreen()->geometry();
    m_dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
    const QRect physical(0, 0, qRound(m_screen.width() * m_dpr),
                         qRound(m_screen.height() * m_dpr));
    m_screen = physical;
    m_today = QDate::currentDate();
    m_year = m_today.year();
    m_month = m_today.month();
    clampSettings();
}

CalendarHostWidget::~CalendarHostWidget()
{
    stopDuty();
}

void CalendarHostWidget::clampSettings()
{
    const int w = std::clamp(m_settings.width, 520, std::max(520, m_screen.width()));
    const int h = std::clamp(m_settings.height, 420, std::max(420, m_screen.height()));
    const int x = std::clamp(m_settings.x, m_screen.left(),
                             std::max(m_screen.left(), m_screen.right() + 1 - w));
    const int y = std::clamp(m_settings.y, m_screen.top(),
                             std::max(m_screen.top(), m_screen.bottom() + 1 - h));
    m_settings.x = x;
    m_settings.y = y;
    m_settings.width = w;
    m_settings.height = h;
}

void CalendarHostWidget::startDuty()
{
    if (m_stopped)
        return;
    if (!m_canvas.isValid()) {
        if (!m_canvas.create(m_screen.width(), m_screen.height()))
            return;
    }
    m_settings = CalendarSettings::load();
    m_store = CalendarTaskStore::load();
    clampSettings();
    m_renderer = std::make_unique<CalendarRenderer>(m_screen.width(), m_screen.height(), m_dpr);
    m_canvas.setHostWindow(reinterpret_cast<HWND>(winId()));
    render();

    if (!m_clock) {
        m_clock = new QTimer(this);
        m_clock->setInterval(1000);
        connect(m_clock, &QTimer::timeout, this, &CalendarHostWidget::tickClock);
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
    if (!m_watchdog) {
        m_watchdog = new QTimer(this);
        m_watchdog->setInterval(2000);
        connect(m_watchdog, &QTimer::timeout, this, [this] {
            if (findHookFrameworkWindow() == nullptr)
                startDock();
        });
    }
    m_watchdog->start();
    startDock();
}

void CalendarHostWidget::stopDuty()
{
    m_stopped = true;
    if (m_clock)
        m_clock->stop();
    if (m_wallpaperTimer)
        m_wallpaperTimer->stop();
    if (m_watchdog)
        m_watchdog->stop();
    if (m_stopDock)
        m_stopDock();
}

void CalendarHostWidget::startDock()
{
    if (!m_hookLib) {
        const QString dllPath = QDir(QCoreApplication::applicationDirPath())
                                    .absoluteFilePath(QStringLiteral("PopToolsCalendarHook.dll"));
        m_hookLib = new QLibrary(dllPath, this);
        if (!m_hookLib->load()) {
            qWarning("CalendarHost: 加载注入 DLL 失败: %s",
                     qUtf8Printable(m_hookLib->errorString()));
            return;
        }
        m_startDock = reinterpret_cast<int (*)(HWND)>(m_hookLib->resolve("StartDock"));
        m_stopDock = reinterpret_cast<int (*)()>(m_hookLib->resolve("StopDock"));
    }
    if (!m_startDock)
        return;
    m_canvas.setHostWindow(reinterpret_cast<HWND>(winId()));
    m_startDock(reinterpret_cast<HWND>(winId()));
    notifyHookUpdate();
}

void CalendarHostWidget::notifyHookUpdate()
{
    const HWND fw = findHookFrameworkWindow();
    if (fw)
        ::PostMessageW(fw, kMsgUpdate, 0, 0);
}

void CalendarHostWidget::render()
{
    if (m_adjusting || !m_renderer || !m_canvas.isValid())
        return;
    QPainter g(&m_canvas.image());
    m_layout = m_renderer->render(&g, m_canvas, m_settings, m_store, m_today, m_year, m_month,
                                  m_hover, m_hoverHint, false);
    notifyHookUpdate();
}

void CalendarHostWidget::renderDuringAdjust()
{
    if (!m_renderer || !m_canvas.isValid())
        return;
    QPainter g(&m_canvas.image());
    m_layout = m_renderer->render(&g, m_canvas, m_settings, m_store, m_today, m_year, m_month,
                                  QPoint(-9999, -9999), QString(), false);
    notifyHookUpdate();
}

void CalendarHostWidget::tickClock()
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

QPoint CalendarHostWidget::logicalPos(const QPoint &physical) const
{
    if (m_dpr <= 0.0)
        return physical;
    return QPoint(qRound(physical.x() / m_dpr), qRound(physical.y() / m_dpr));
}

bool CalendarHostWidget::nativeEvent(const QByteArray &eventType, void *message, qint64 *result)
{
    Q_UNUSED(eventType)
    Q_UNUSED(result)
    MSG *msg = static_cast<MSG *>(message);
    if (msg->message == kMsgEvent) {
        if (!m_modal && !m_adjusting) {
            const int ev = int(msg->wParam & 0xFFFF);
            const int extra = int((msg->wParam >> 16) & 0xFFFF);
            const int x = short(msg->lParam & 0xFFFF);
            const int y = short((msg->lParam >> 16) & 0xFFFF);
            onDllEvent(ev, extra, QPoint(x, y));
        }
        return true;
    }
    return QWidget::nativeEvent(eventType, message, result);
}

void CalendarHostWidget::onDllEvent(int ev, int extra, const QPoint &p)
{
    switch (ev) {
        case kEvMove:
            updateHover(p);
            break;
        case kEvLeave:
            if (m_hover != QPoint(-9999, -9999) || !m_hoverHint.isEmpty()) {
                m_hover = QPoint(-9999, -9999);
                m_hoverHint.clear();
                m_lastHeaderHover = -1;
                render();
            }
            break;
        case kEvDown:
            m_mouseDown = p;
            break;
        case kEvUp:
            if (extra != 0) { // 点在图标上：不处理
                m_mouseDown.reset();
                return;
            }
            if (m_mouseDown.has_value()) {
                const QPoint down = m_mouseDown.value();
                m_mouseDown.reset();
                if (std::abs(down.x() - p.x()) <= 4 && std::abs(down.y() - p.y()) <= 4)
                    handleSingleClick(p);
            }
            break;
        case kEvDblclk:
            if (extra != 0)
                return;
            handleDoubleClick(p);
            break;
        default:
            break;
    }
}

void CalendarHostWidget::updateHover(const QPoint &p)
{
    if (!m_layout.has_value()) {
        render();
        if (!m_layout.has_value())
            return;
    }
    m_hover = p;
    QString hint;
    const int hh = m_layout->hitHeader(p);
    if (hh >= 0) {
        switch (CalendarLayout::headerKinds[hh]) {
            case HeaderButtonKind::Settings: hint = QStringLiteral("日历设置"); break;
            case HeaderButtonKind::Plugin: hint = QStringLiteral("插件"); break;
            case HeaderButtonKind::Previous: hint = QStringLiteral("上一页"); break;
            case HeaderButtonKind::Next: hint = QStringLiteral("下一页"); break;
            case HeaderButtonKind::Menu: hint = QStringLiteral("更多"); break;
        }
    } else if (m_settings.showGuide && m_layout->guidePill.contains(p)) {
        hint = QStringLiteral("调整日历清单的位置和大小");
    }
    if (hh != m_lastHeaderHover || hint != m_hoverHint) {
        m_lastHeaderHover = hh;
        m_hoverHint = hint;
        render();
    }
}

void CalendarHostWidget::handleSingleClick(const QPoint &p)
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
            case HeaderButtonKind::Menu:
                openMainMenu();
                break;
            case HeaderButtonKind::Settings:
                openSettings();
                break;
            case HeaderButtonKind::Plugin:
                openPluginMenu();
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

void CalendarHostWidget::handleDoubleClick(const QPoint &p)
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

void CalendarHostWidget::openEditor(const QDate &date, const QRect &cell)
{
    m_modal = true;
    const int w = 380, h = 430;
    int x = cell.left() + 12;
    if (x + w > m_screen.right() + 1 - 12)
        x = cell.left() + cell.width() - w - 12;
    int y = cell.top() + 12;
    if (y + h > m_screen.bottom() + 1 - 60)
        y = m_screen.bottom() + 1 - h - 60;
    x = std::max(m_screen.left() + 10, x);
    y = std::max(m_screen.top() + 10, y);
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

void CalendarHostWidget::openMainMenu()
{
    if (!m_layout.has_value())
        return;
    const QRect b = m_layout->headerButtons.at(4);
    int x = std::clamp(b.left() + b.width() - 200, m_screen.left() + 6,
                       m_screen.right() + 1 - 206);
    int y = std::clamp(b.bottom() + 1 + 4, m_screen.top() + 6, m_screen.bottom() + 1 - 500);
    std::vector<CalendarMenuItemDef> items = {
        { QStringLiteral("内容搜索"), [this] { searchTasks(); }, false, false, false },
        { QStringLiteral("选择日期"), [this] { pickDate(); }, false, false, false },
        { QStringLiteral("主题皮肤"),
          [this] {
              showInfo(QStringLiteral("主题皮肤"),
                       QStringLiteral("默认深色毛玻璃皮肤。\n可在日历设置中调整毛玻璃与字体大小。"));
          },
          true, false, false },
        { QStringLiteral("调整位置和大小"), [this] { enterAdjustMode(); }, false, false, false },
        { QStringLiteral("隐藏日历清单"), [this] {
             m_settings.hidden = true;
             m_settings.save();
             render();
         },
         false, false, false },
        { QStringLiteral("隐藏日历内容"),
          [this] {
              m_settings.showEvents = !m_settings.showEvents;
              m_settings.save();
              render();
          },
          true, true, !m_settings.showEvents },
        { QStringLiteral("导出"), [this] { exportTasks(); }, false, false, false },
        { QStringLiteral("导入"), [this] { importTasks(); }, false, false, false },
        { QStringLiteral("打印"), [this] { showInfo(QStringLiteral("打印"), QStringLiteral("暂未实现打印。")); },
          true, false, false },
        { QStringLiteral("日历设置"), [this] { openSettings(); }, false, false, false },
        { QStringLiteral("功能介绍"),
          [this] {
              showInfo(QStringLiteral("功能介绍"),
                       QStringLiteral("桌面日历：\n· 日历贴在壁纸上、图标下方\n· 不影响右键、框选和图标拖动\n"
                                      "· 双击日期编辑当日日程\n· 支持农历、节气、节日和周数"));
          },
          false, false, false },
        { QStringLiteral("关于"), [this] { showInfo(QStringLiteral("关于"), QStringLiteral("桌面日历 v1.0（PopTools 模块）")); },
          true, false, false },
        { QStringLiteral("退出软件"), [this] {
             stopDuty();
             qApp->quit();
         },
         false, false, false }
    };
    auto *menu = new CalendarMenuForm(std::move(items), logicalPos(QPoint(x, y)), nullptr);
    trackModal(menu);
    menu->show();
    menu->raise();
}

void CalendarHostWidget::openPluginMenu()
{
    if (!m_layout.has_value())
        return;
    m_modal = true;
    const QRect b = m_layout->headerButtons.at(1);
    int x = std::clamp(b.left() + b.width() - 200, m_screen.left() + 6,
                       m_screen.right() + 1 - 206);
    int y = std::clamp(b.bottom() + 1 + 4, m_screen.top() + 6, m_screen.bottom() + 1 - 330);
    auto tip = [this](const QString &title, const QString &body) {
        showInfo(title, body);
    };
    std::vector<CalendarMenuItemDef> items = {
        { QStringLiteral("桌面管理"),
          [tip] { tip(QStringLiteral("桌面管理"), QStringLiteral("未实现桌面管理插件。")); },
          false, false, false },
        { QStringLiteral("新建便签"),
          [tip] { tip(QStringLiteral("新建便签"), QStringLiteral("暂未实现便签。")); },
          false, false, false },
        { QStringLiteral("便签设置"),
          [tip] { tip(QStringLiteral("便签设置"), QStringLiteral("暂未实现便签。")); },
          false, false, false },
        { QStringLiteral("便签管理"),
          [tip] { tip(QStringLiteral("便签管理"), QStringLiteral("暂未实现便签。")); },
          false, false, false },
        { QStringLiteral("新建课程表"),
          [tip] { tip(QStringLiteral("课程表"), QStringLiteral("暂未实现课程表。")); },
          false, false, false },
        { QStringLiteral("AI工作平台"),
          [tip] { tip(QStringLiteral("AI工作平台"), QStringLiteral("暂未实现。")); },
          false, false, false },
        { QStringLiteral("三方日历设置"),
          [tip] { tip(QStringLiteral("三方日历"), QStringLiteral("暂未实现三方日历。")); },
          false, false, false },
        { QStringLiteral("自动生成图标"),
          [tip] { tip(QStringLiteral("自动生成图标"), QStringLiteral("暂未实现。")); },
          false, false, false }
    };
    auto *menu = new CalendarMenuForm(std::move(items), logicalPos(QPoint(x, y)), nullptr);
    trackModal(menu);
    menu->show();
    menu->raise();
}

void CalendarHostWidget::centerOnPanel(QWidget *w)
{
    // 弹窗锚定日历面板中心（无 parent 的 QDialog 默认落在"活动屏"，多屏机器上
    // 曾开到副屏导致用户以为日历无响应）。物理 → 逻辑换算见坐标约定
    const QRect panel = m_settings.rect();
    const QPoint tl = logicalPos(QPoint(panel.left(), panel.top()));
    const QPoint br = logicalPos(QPoint(panel.right(), panel.bottom()));
    w->move(tl.x() + (br.x() - tl.x() - w->width()) / 2,
            tl.y() + (br.y() - tl.y() - w->height()) / 2);
}

void CalendarHostWidget::openSettings()
{
    auto *form = new CalendarSettingsForm(m_settings, nullptr);
    // QDialog::done()（确定/取消/关窗/ESC）只 hide 不析构——QObject::destroyed
    // 永不触发，m_modal 会永久卡死（日历冻结 bug 的根因）。改监听 finished
    //（done() 必发），收尾后 deleteLater，复位交给 trackModal 的 destroyed 守卫
    connect(form, &QDialog::finished, this, [this, form](int result) {
        if (result == QDialog::Accepted) {
            m_settings.save();
            if (m_renderer)
                m_renderer->refreshWallpaper();
        }
        form->deleteLater();
    });
    trackModal(form);
    centerOnPanel(form);
    form->show();
    form->raise();
    form->activateWindow();
}

void CalendarHostWidget::enterAdjustMode()
{
    if (m_adjusting)
        return;
    m_adjusting = true;
    m_settings.showGuide = false;
    const QRect original = m_settings.rect();
    auto *overlay =
        new CalendarAdjustOverlay(original, m_screen, m_dpr,
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

void CalendarHostWidget::showCalendar()
{
    m_stopped = false;
    m_settings.hidden = false;
    m_settings.save();
    if (!m_renderer || !m_canvas.isValid())
        startDuty();
    else
        render();
}

void CalendarHostWidget::searchTasks()
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

void CalendarHostWidget::pickDate()
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

void CalendarHostWidget::exportTasks()
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

void CalendarHostWidget::importTasks()
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

void CalendarHostWidget::showInfo(const QString &title, const QString &text)
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

void CalendarHostWidget::trackModal(QWidget *w)
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
