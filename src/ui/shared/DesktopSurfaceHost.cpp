#include "DesktopSurfaceHost.h"

#include <QApplication>
#include <QDir>
#include <QGuiApplication>
#include <QLibrary>
#include <QPainter>
#include <QScreen>
#include <QTimer>

#include <cstdlib>

// 与注入 DLL 的协议常量一致（见 src/hook/calendar_hook.cpp）
namespace {
constexpr UINT kMsgUpdate = WM_APP + 0x101;
constexpr UINT kMsgEvent = WM_APP + 0x103;
constexpr int kEvMove = 1;
constexpr int kEvDown = 2;
constexpr int kEvUp = 3;
constexpr int kEvDblclk = 4;
constexpr int kEvLeave = 5;
} // namespace

namespace poptools {

DesktopSurfaceHost::DesktopSurfaceHost(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
{
    setObjectName("DesktopSurfaceHost");
    setAttribute(Qt::WA_ShowWithoutActivating);
    // 隐藏宿主窗口：1x1 放屏幕外，仅接收 DLL 转发的鼠标事件消息
    setGeometry(-32000, -32000, 1, 1);
    show();

    m_screen = QGuiApplication::primaryScreen()->geometry();
    m_dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
    m_screen = QRect(0, 0, qRound(m_screen.width() * m_dpr), qRound(m_screen.height() * m_dpr));
}

DesktopSurfaceHost::~DesktopSurfaceHost()
{
    stopDuty();
    for (const PanelSlot &slot : m_panels) {
        if (slot.panel)
            slot.panel->attachSurface(nullptr);
    }
}

int DesktopSurfaceHost::registerPanel(DesktopPanel *panel)
{
    if (!panel)
        return -1;
    for (const PanelSlot &slot : m_panels) {
        if (slot.panel == panel)
            return slot.id;
    }
    panel->attachSurface(this);
    const int id = m_nextPanelId++;
    m_panels.append({ id, panel });
    // 面板在值守开启后注册：补发启停通知与首帧（正常时序是先注册后 startDuty）
    if (m_dutyStarted) {
        panel->onDutyStarted();
        panel->render(); // 契约：render() 末尾自行 commitSurface()
    }
    return id;
}

void DesktopSurfaceHost::unregisterPanel(int id)
{
    for (int i = 0; i < m_panels.size(); ++i) {
        if (m_panels[i].id == id) {
            if (m_downPanel == m_panels[i].panel)
                m_downPanel = nullptr;
            if (m_panels[i].panel)
                m_panels[i].panel->attachSurface(nullptr);
            m_panels.removeAt(i);
            return;
        }
    }
}

void DesktopSurfaceHost::startDuty()
{
    if (m_stopped)
        return;
    if (!m_canvas.isValid()) {
        if (!m_canvas.create(m_screen.width(), m_screen.height()))
            return;
    }
    if (!m_dutyStarted) {
        m_dutyStarted = true;
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
        for (const PanelSlot &slot : m_panels) {
            if (slot.panel) {
                slot.panel->onDutyStarted();
                slot.panel->render();
            }
        }
        notifyHookUpdate();
    }
}

void DesktopSurfaceHost::stopDuty()
{
    m_stopped = true;
    if (m_watchdog)
        m_watchdog->stop();
    for (const PanelSlot &slot : m_panels) {
        if (slot.panel)
            slot.panel->onDutyStopped();
    }
    if (m_stopDock)
        m_stopDock();
    m_dutyStarted = false;
}

void DesktopSurfaceHost::resumeDuty()
{
    m_stopped = false;
    startDuty();
}

void DesktopSurfaceHost::commitSurface(const QRect &dirtyPhysical)
{
    if (!m_canvas.isValid())
        return;
    const QRect full(QPoint(0, 0), m_canvas.image().size());
    const QRect dirty = dirtyPhysical.isEmpty() ? full : dirtyPhysical.intersected(full);
    if (dirty.isEmpty())
        return;
    if (m_composite.size() != m_canvas.image().size()
        || m_composite.format() != m_canvas.image().format())
        m_composite = QImage(m_canvas.image().size(), m_canvas.image().format());

    // 合成：清透明 → 依注册顺序叠各面板最近整帧（面板帧自上次 commit 后
    // 未变也照抄，保证位图总是完整一致的状态）。脏矩形路径带裁剪只动脏区
    QPainter g(&m_composite);
    g.setClipRect(dirty);
    g.setCompositionMode(QPainter::CompositionMode_Source);
    g.fillRect(dirty, Qt::transparent);
    g.setCompositionMode(QPainter::CompositionMode_SourceOver);
    for (const PanelSlot &slot : m_panels) {
        if (slot.panel) {
            if (QImage *f = slot.panel->frame())
                g.drawImage(0, 0, *f);
        }
    }
    g.end();

    // 共享头矩形槽：按注册序写入（槽 0..kMaxSlots-1），多余槽写空矩形
    for (int i = 0; i < DesktopSharedCanvas::kMaxSlots; ++i) {
        const bool has = i < m_panels.size() && m_panels.at(i).panel;
        m_canvas.setSlotRect(i, has ? m_panels.at(i).panel->rectPhysical() : QRect());
    }
    m_canvas.publishFrame(m_composite, dirty);
    m_canvas.markReady();
    notifyHookUpdate();
}

void DesktopSurfaceHost::centerOnPanelRect(QWidget *w, const QRect &panelPhysical) const
{
    const QPoint tl = QPoint(qRound(panelPhysical.left() / (m_dpr > 0 ? m_dpr : 1.0)),
                             qRound(panelPhysical.top() / (m_dpr > 0 ? m_dpr : 1.0)));
    const QPoint br = QPoint(qRound(panelPhysical.right() / (m_dpr > 0 ? m_dpr : 1.0)),
                             qRound(panelPhysical.bottom() / (m_dpr > 0 ? m_dpr : 1.0)));
    w->move(tl.x() + (br.x() - tl.x() - w->width()) / 2,
            tl.y() + (br.y() - tl.y() - w->height()) / 2);
}

bool DesktopSurfaceHost::nativeEvent(const QByteArray &eventType, void *message, qint64 *result)
{
    Q_UNUSED(eventType)
    Q_UNUSED(result)
    MSG *msg = static_cast<MSG *>(message);
    if (msg->message == kMsgEvent) {
        const int ev = int(msg->wParam & 0xFFFF);
        const int extra = int((msg->wParam >> 16) & 0xFFFF);
        const int x = short(msg->lParam & 0xFFFF);
        const int y = short((msg->lParam >> 16) & 0xFFFF);
        const QPoint p(x, y);
        switch (ev) {
            case kEvMove:
                for (const PanelSlot &slot : m_panels) {
                    if (slot.panel)
                        slot.panel->onMove(p);
                }
                break;
            case kEvLeave:
                for (const PanelSlot &slot : m_panels) {
                    if (slot.panel)
                        slot.panel->onLeave();
                }
                break;
            case kEvDown:
                m_downPanel = hitPanel(p);
                m_downPoint = p;
                if (m_downPanel)
                    m_downPanel->onDown(p);
                break;
            case kEvUp:
                // extra!=0：点在桌面图标上，穿透给系统，不消化
                if (extra != 0) {
                    m_downPanel = nullptr;
                    break;
                }
                if (m_downPanel && std::abs(m_downPoint.x() - p.x()) <= 4
                    && std::abs(m_downPoint.y() - p.y()) <= 4) {
                    DesktopPanel *target = m_downPanel;
                    m_downPanel = nullptr;
                    // unregisterPanel 时已同步清 m_downPanel，仍登记着即有效
                    bool stillRegistered = false;
                    for (const PanelSlot &slot : m_panels) {
                        if (slot.panel == target)
                            stillRegistered = true;
                    }
                    if (stillRegistered)
                        target->onClick(p);
                } else {
                    m_downPanel = nullptr;
                }
                break;
            case kEvDblclk:
                if (extra != 0)
                    break;
                if (DesktopPanel *panel = hitPanel(p))
                    panel->onDoubleClick(p);
                break;
            default:
                break;
        }
        return true;
    }
    return QWidget::nativeEvent(eventType, message, result);
}

DesktopPanel *DesktopSurfaceHost::hitPanel(const QPoint &physical) const
{
    // 后注册者优先：合成时后注册面板画在更上层（收纳盒叠日历之上），命中
    // 顺序与视觉层叠保持一致——重叠区点击/双击由上层面板响应
    for (int i = m_panels.size() - 1; i >= 0; --i) {
        const PanelSlot &slot = m_panels.at(i);
        if (slot.panel && slot.panel->rectPhysical().contains(physical))
            return slot.panel;
    }
    return nullptr;
}

void DesktopSurfaceHost::startDock()
{
    if (!m_hookLib) {
        const QString dllPath = QDir(QCoreApplication::applicationDirPath())
                                    .absoluteFilePath(QStringLiteral("PopToolsCalendarHook.dll"));
        m_hookLib = new QLibrary(dllPath, this);
        if (!m_hookLib->load()) {
            qWarning("DesktopSurface: 加载注入 DLL 失败: %s",
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

void DesktopSurfaceHost::notifyHookUpdate()
{
    const HWND fw = findHookFrameworkWindow();
    if (fw)
        ::PostMessageW(fw, kMsgUpdate, 0, 0);
}

HWND DesktopSurfaceHost::findHookFrameworkWindow()
{
    return ::FindWindowW(L"PopToolsCalendarHook_Framework", L"PopToolsCalendarHook_Title");
}

} // namespace poptools
