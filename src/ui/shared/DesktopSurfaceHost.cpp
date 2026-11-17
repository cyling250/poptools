#include "DesktopSurfaceHost.h"

#include <QApplication>
#include <QDir>
#include <QGuiApplication>
#include <QLibrary>
#include <QPainter>
#include <QScreen>
#include <QTimer>

#include <cstdlib>

#include "hook/desktop_protocol.h" // 跨进程协议常量（与注入 DLL 同源，S-01）
#include "DesktopScreens.h" // 多屏几何 helper（虚拟桌面并集/归属屏解析）

namespace poptools {

// 1x1 屏外宿主窗：永不激活不显示，仅作 DLL 事件投递目标；主屏物理尺寸/DPR
// 构造时快照（运行中改分辨率不跟随，登记 BUG-08，修复超出卫生范畴）
DesktopSurfaceHost::DesktopSurfaceHost(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
{
    setObjectName("DesktopSurfaceHost");
    setAttribute(Qt::WA_ShowWithoutActivating);
    // 隐藏宿主窗口：1x1 放屏幕外，仅接收 DLL 转发的鼠标事件消息
    setGeometry(-32000, -32000, 1, 1);
    show();

    // 画布覆盖整个虚拟桌面（并集物理像素）：面板槽矩形即虚拟桌面坐标，面板
    // 跨屏/归属切换只改槽矩形，画布无需重建（T-063 多屏移植）
    m_screen = deskscreens::virtualDesktopPhysical();
    m_dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
}

// 停值守（解除注入）并解除面板反引；QLibrary 随 QObject 父子关系析构
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
        if (!m_canvas.create(m_screen.width(), m_screen.height())) {
            // T-062 修复: BUG-02 段名不含尺寸——explorer 侧 DLL 长期持有同名旧段
            //（仅 STOP/explorer 重启才释放），更高分辨率下重开本应用时 create 必败，
            // 且此路径在 watchdog 创建之前、无任何重试 → 面板静默消失直到 explorer
            // 重启。保守方案（不改段名/协议格式）：先惰性加载 DLL（首次启动 explorer
            // 里也可能残留上一会话的旧段持有者），再 StopDock 请求 explorer 清理旧段，
            // 延时（非阻塞 UI 线程，比报告的轮询等待更保守）重试一次创建，成功即续走
            // 正常值守流程；仍失败放弃并告警
            ensureHookLib();
            if (m_stopDock) {
                m_stopDock(); // 异步：explorer 收 WM_CLONE_STOP 后释放旧段
                QTimer::singleShot(1500, this, [this] {
                    if (m_stopped || m_dutyStarted || m_canvas.isValid())
                        return;
                    if (m_canvas.create(m_screen.width(), m_screen.height()))
                        startDuty();
                    else
                        qWarning("DesktopSurface: 共享画布二次创建仍失败(%dx%d)，放弃本次值守",
                                 m_screen.width(), m_screen.height());
                });
            }
            return;
        }
    }
    if (!m_dutyStarted) {
        m_dutyStarted = true;
        if (!m_watchdog) {
            m_watchdog = new QTimer(this);
            m_watchdog->setInterval(2000);
            connect(m_watchdog, &QTimer::timeout, this, [this] {
                // 自愈判据 = DLL 消息窗存在性：explorer 重启后窗口消失 → 重装钩子
                if (findHookFrameworkWindow() == nullptr)
                    startDock();
                // 屏拓扑自愈（拔屏/改分辨率，T-063 移植）：虚拟桌面并集变化 →
                // 画布按新尺寸重建，全面板重建渲染器并重绘
                const QRect vdesk = deskscreens::virtualDesktopPhysical();
                if (!vdesk.isEmpty() && vdesk != m_screen && m_canvas.isValid()) {
                    m_screen = vdesk;
                    m_canvas.create(m_screen.width(), m_screen.height());
                    m_canvas.setHostWindow(reinterpret_cast<HWND>(winId()));
                    for (const PanelSlot &slot : m_panels) {
                        if (slot.panel) {
                            slot.panel->onDutyStopped();
                            slot.panel->onDutyStarted();
                            slot.panel->render();
                        }
                    }
                    notifyHookUpdate();
                }
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
    // m_composite 惰性重建：尺寸/格式没变就复用——脏矩形路径只改局部，其余区域
    // 保留上次内容（= 共享位图现状），这是"位图总是完整一致"的前提
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
        // 空槽矩形面板（调整模式隐藏等）不参与合成——槽矩形与合成一致地"缺席"，
        // 桌面层即无该面板（T-063 覆盖层预览拖动的桌面隐藏机制）
        if (slot.panel && !slot.panel->rectPhysical().isNull()) {
            if (QImage *f = slot.panel->frame())
                g.drawImage(0, 0, *f);
        }
    }
    g.end();

    m_canvas.publishFrame(m_composite, dirty);
    // 共享头矩形槽：按注册序写入（槽 0..kMaxSlots-1），多余槽写空矩形。
    // T-062 修复: BUG-04 槽矩形改在位图发布之后、markReady 之前写入——原顺序在
    // 跨进程竞态下"头新图旧"（DLL 读到新矩形但位图还是旧内容，新位置闪现旧
    // 画面，更显眼），重排后收窄为"图新头旧"（旧位置短暂残影，DLL 残影失效
    // 逻辑本就兜底）。行为等价的纯重排
    for (int i = 0; i < DesktopSharedCanvas::kMaxSlots; ++i) {
        const bool has = i < m_panels.size() && m_panels.at(i).panel;
        m_canvas.setSlotRect(i, has ? m_panels.at(i).panel->rectPhysical() : QRect());
    }
    m_canvas.markReady();
    notifyHookUpdate();
}

void DesktopSurfaceHost::centerOnPanelRect(QWidget *w, const QRect &panelPhysical) const
{
    // 用 right()/bottom() 比 left+width 少 1 物理像素，弹窗居中偏差
    // ≤ 1/(2·dpr) 逻辑 px（150% 下约 0.33px）；如需严格可改 topLeft()+size() 换算
    const QPoint tl = QPoint(qRound(panelPhysical.left() / (m_dpr > 0 ? m_dpr : 1.0)),
                             qRound(panelPhysical.top() / (m_dpr > 0 ? m_dpr : 1.0)));
    const QPoint br = QPoint(qRound(panelPhysical.right() / (m_dpr > 0 ? m_dpr : 1.0)),
                             qRound(panelPhysical.bottom() / (m_dpr > 0 ? m_dpr : 1.0)));
    w->move(tl.x() + (br.x() - tl.x() - w->width()) / 2,
            tl.y() + (br.y() - tl.y() - w->height()) / 2);
}

// DLL 转发鼠标事件入口：解码口径见 calendar_hook.cpp ForwardEvent（wParam 低
// 16 位=事件号、高 16 位=extra、lParam=屏幕坐标 MAKELPARAM 符号扩展）。
// move/leave 广播全面板（跨面板清 hover）；down 记配对面板，up 以 ≤4px 物理
// 位移判单击且仍注册才派发；点在桌面图标上（extra!=0）穿透不消化
bool DesktopSurfaceHost::nativeEvent(const QByteArray &eventType, void *message, qint64 *result)
{
    Q_UNUSED(eventType)
    Q_UNUSED(result)
    MSG *msg = static_cast<MSG *>(message);
    if (msg->message == POPTOOLS_MSG_CLONE_EVENT) {
        const int ev = int(msg->wParam & 0xFFFF);
        const int extra = int((msg->wParam >> 16) & 0xFFFF);
        const int x = short(msg->lParam & 0xFFFF);
        const int y = short((msg->lParam >> 16) & 0xFFFF);
        const QPoint p(x, y);
        switch (ev) {
            case POPTOOLS_EV_MOVE:
                for (const PanelSlot &slot : m_panels) {
                    if (slot.panel)
                        slot.panel->onMove(p);
                }
                break;
            case POPTOOLS_EV_LEAVE:
                for (const PanelSlot &slot : m_panels) {
                    if (slot.panel)
                        slot.panel->onLeave();
                }
                break;
            case POPTOOLS_EV_DOWN:
                m_downPanel = hitPanel(p);
                m_downPoint = p;
                if (m_downPanel)
                    m_downPanel->onDown(p);
                break;
            case POPTOOLS_EV_UP:
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
            case POPTOOLS_EV_DBLCLK:
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

// 惰性加载注入 DLL 并解析导出（只加载一次；加载/解析失败即丢弃，见 BUG-01 修复注释）
bool DesktopSurfaceHost::ensureHookLib()
{
    if (m_hookLib && m_startDock)
        return true;
    if (!m_hookLib) {
        const QString dllPath = QDir(QCoreApplication::applicationDirPath())
                                    .absoluteFilePath(QStringLiteral("PopToolsCalendarHook.dll"));
        m_hookLib = new QLibrary(dllPath, this);
        if (!m_hookLib->load()) {
            qWarning("DesktopSurface: 加载注入 DLL 失败: %s",
                     qUtf8Printable(m_hookLib->errorString()));
            // T-062 修复: BUG-01 加载失败后若保留 m_hookLib，看门狗每 2s 调本函数
            // 都在 if(!m_hookLib) 短路、永不重试——首次加载失败（DLL 未拷齐/被杀软
            // 占用/部署时序）会让面板静默消失整个值守周期。失败即丢弃，看门狗
            // 下个周期自动重载
            m_hookLib->deleteLater();
            m_hookLib = nullptr;
            return false;
        }
        m_startDock = reinterpret_cast<int (*)(HWND)>(m_hookLib->resolve("StartDock"));
        m_stopDock = reinterpret_cast<int (*)()>(m_hookLib->resolve("StopDock"));
        if (!m_startDock || !m_stopDock) {
            qWarning("DesktopSurface: 注入 DLL 缺少 StartDock/StopDock 导出: %s",
                     qUtf8Printable(m_hookLib->errorString()));
            // T-062 修复: BUG-01 同上——resolve 失败也必须丢弃，否则看门狗永久短路
            m_hookLib->deleteLater();
            m_hookLib = nullptr;
            m_startDock = nullptr;
            m_stopDock = nullptr;
            return false;
        }
    }
    return m_startDock != nullptr;
}

// 加载注入 DLL 并 StartDock；setHostWindow 与 DLL 内写头部互为冗余保险
void DesktopSurfaceHost::startDock()
{
    if (!ensureHookLib())
        return;
    m_canvas.setHostWindow(reinterpret_cast<HWND>(winId()));
    m_startDock(reinterpret_cast<HWND>(winId()));
    notifyHookUpdate();
}

// 投递 WM_APP+0x101 让 DLL 逐槽失效重绘；消息窗不存在（未注入/已停）静默跳过
void DesktopSurfaceHost::notifyHookUpdate()
{
    const HWND fw = findHookFrameworkWindow();
    if (fw)
        ::PostMessageW(fw, POPTOOLS_MSG_CLONE_UPDATE, 0, 0);
}

void DesktopSurfaceHost::requestFullRepaint()
{
    const HWND fw = findHookFrameworkWindow();
    if (fw)
        ::PostMessageW(fw, POPTOOLS_MSG_CLONE_UPDATE, 1, 0);
}

// 跨进程查 DLL 消息窗（类名/标题须与 calendar_hook.cpp EnsureInit 一致）；
// nullptr = 注入未生效
HWND DesktopSurfaceHost::findHookFrameworkWindow()
{
    return ::FindWindowW(L"PopToolsCalendarHook_Framework", L"PopToolsCalendarHook_Title");
}

} // namespace poptools
