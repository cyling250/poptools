#include "ActivityMonitorModule.h"

#include <QAction>
#include <QDateTime>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>

#include "../AppServices.h"
#include "core/hotkey/GlobalMouseHook.h"
#include "core/monitor/ActivityStore.h"
#include "core/monitor/ClipboardListener.h"
#include "core/monitor/ForegroundTracker.h"
#include "core/monitor/GlobalKeyboardHook.h"
#include "core/monitor/ImeCommitDetector.h"
#include "core/monitor/InputActivityMonitor.h"
#include "core/monitor/SnapshotOcrPipeline.h"
#include "core/monitor/TextMirrorMonitor.h"
#include "core/settings/Settings.h"

namespace poptools {

namespace {
// 事件行公共字段：epoch ms 时间戳 + 前台窗口上下文
QJsonObject baseEvent(const QString &app, const QString &title)
{
    QJsonObject o;
    o.insert("ts", QDateTime::currentMSecsSinceEpoch());
    o.insert("app", app);
    o.insert("title", title);
    return o;
}
} // namespace

// 无状态：全部运行期对象在 startMonitor 建、stopMonitor 销
ActivityMonitorModule::ActivityMonitorModule(QObject *parent)
    : QObject(parent)
{
}

void ActivityMonitorModule::init(AppServices &services)
{
    m_services = &services;
    connect(&services, &AppServices::settingsChanged, this,
            &ActivityMonitorModule::applyRunningState);

    // 托盘一键启停（设置页之外的快捷入口；文案随状态翻转）
    m_trayAction = services.addTrayAction(QStringLiteral("活动监控"), [this] { toggleFromTray(); });

    // 事件循环就绪后再开钩子（init 早于 app.exec）；无条件调用以刷新
    // 托盘文案（enabled=false 时只改文案不建任何运行期对象）。
    // 上下文用 this 而非 app 实例：singleShot 的 QObject 重载即支持成员
    // 上下文，模块若先于触发亡则回调自动丢弃（原 app 上下文下会悬垂），
    // 且与本文件其余回调一律以 this 为上下文的口径一致
    QTimer::singleShot(0, this, [this] { applyRunningState(); });
}

void ActivityMonitorModule::shutdown()
{
    stopMonitor();
}

void ActivityMonitorModule::applyRunningState()
{
    const bool want = Settings::instance().monitorEnabled();
    if (want && !m_store)
        startMonitor();
    else if (!want && m_store)
        stopMonitor();
    else if (want && m_store) {
        // 参数变化即时生效：保留天数 / 空闲阈值 / OCR 快照开关任一改动都
        // 重启监控——三项均在 start 时注入（prune 只在 store start 时执行，
        // idle 阈值在 monitor 构造时注入，OCR 链按开关装配），重启路径经
        // stop() 的 flushBlocking，不丢缓冲
        const Settings &s = Settings::instance();
        if (s.monitorRetentionDays() != m_appliedRetentionDays
            || s.monitorIdleThresholdSec() != m_appliedIdleThresholdSec
            || s.monitorOcrSnapshot() != m_appliedOcrSnapshot) {
            stopMonitor();
            startMonitor();
        }
    }
    if (m_trayAction)
        m_trayAction->setText(want ? QStringLiteral("活动监控：运行中（点击暂停）")
                                   : QStringLiteral("活动监控：已暂停（点击开启）"));
}

// 托盘一键启停：翻转设置持久化 → applyRunningState 执行/暂停 → 气泡告知结果
void ActivityMonitorModule::toggleFromTray()
{
    Settings &s = Settings::instance();
    s.setMonitorEnabled(!s.monitorEnabled());
    applyRunningState();
    if (m_services && m_services->notify) {
        m_services->notify(QStringLiteral("活动监控"),
                           s.monitorEnabled()
                               ? QStringLiteral("已开启：键入与窗口活动将记录到本地日志")
                               : QStringLiteral("已暂停：不再记录新的活动"));
    }
}

// 建全套运行期对象并接线（钩子→状态机→落盘→心跳），最后启动钩子；
// 可注入参数（保留天数/空闲阈值/OCR 开关）在此快照到 m_applied* 供重启判定
void ActivityMonitorModule::startMonitor()
{
    if (m_store)
        return;
    const Settings &s = Settings::instance();

    m_store = new ActivityStore(QString(), this);
    // 保留天数须在 start 前设置（prune 只在启动时执行一次）
    m_appliedRetentionDays = s.monitorRetentionDays();
    m_store->setRetentionDays(m_appliedRetentionDays);
    m_store->start();

    m_activity = new InputActivityMonitor(this);
    m_appliedIdleThresholdSec = s.monitorIdleThresholdSec();
    m_activity->setIdleThresholdSec(m_appliedIdleThresholdSec);

    m_kbHook = new GlobalKeyboardHook(this);
    m_tracker = new ForegroundTracker(this);
    m_mouseHook = new GlobalMouseHook(this);

    // 键盘事件：down 记活跃度并触发文本镜像（T-066 起，字母键序流已移除，
    // 击键内容经 UIA 焦点控件镜像以最终文本形态落盘 textlog.jsonl）
    connect(m_kbHook, &GlobalKeyboardHook::keyEvent, this,
            [this](int vk, int scan, bool ext, bool down, quintptr focusHwnd) {
                if (down) {
                    m_activity->noteKeydown();
                    m_mirror->onTypingActivity(); // 文本镜像触发（零耗时入防抖）
                    if (m_imeDetector)
                        m_imeDetector->onKeyEvent(vk, down);
                }
            });

    // ---- T-066 UIA 文本镜像：输入与回退的原文抄录 → textlog.jsonl ----
    // 触发源：键盘 down（上方接线）/ 剪贴板外部变化（粘贴）/ 切窗；
    // 防抖、UIA 读、字段影子差分全在 TextMirrorMonitor 内部，此处只落盘
    m_mirror = new TextMirrorMonitor(this);
    connect(m_mirror, &TextMirrorMonitor::changeReady, this,
            [this](const QString &app, const QString &title,
                   const TextMirrorStore::MirrorEvent &ev) {
                QJsonObject o = baseEvent(app, title);
                o.insert("kind", ev.kind);
                if (!ev.text.isEmpty())
                    o.insert("text", ev.text);
                if (!ev.removed.isEmpty())
                    o.insert("removed", ev.removed);
                o.insert("field", ev.field);
                m_store->append(QStringLiteral("textlog"), o);
            });

    // 时间线段 → timeline.jsonl
    connect(m_activity, &InputActivityMonitor::segmentClosed, this,
            [this](const TimelineSegment &seg) {
                QJsonObject o;
                o.insert("start", seg.startMs);
                o.insert("end", seg.endMs);
                o.insert("app", seg.app);
                o.insert("title", seg.title);
                o.insert("activeSec", seg.activeSec);
                o.insert("keystrokes", seg.keystrokes);
                o.insert("clicks", seg.clicks);
                if (seg.idleEnded)
                    o.insert("idleEnded", true);
                m_store->append(QStringLiteral("timeline"), o);
            });

    // 前台窗口变化 → 全链换上下文（状态机 / 文本 / 镜像 / 上屏检测 / OCR
    // 基线 / 剪贴板事件归属）
    connect(m_tracker, &ForegroundTracker::foregroundChanged, this,
            [this](quintptr hwnd, const QString &app, const QString &title) {
                m_activity->setWindowContext(hwnd, app, title);
                m_mirror->setWindowContext(app, title);
                m_mirror->onWindowChanged(); // 新字段当前内容尽快上镜建影子
                if (m_imeDetector)
                    m_imeDetector->reset();
                if (m_ocrPipeline)
                    m_ocrPipeline->setWindowContext(hwnd, app, title);
                if (m_clipboard)
                    m_clipboard->setContext(app, title);
            });

    // ---- 中文上屏取文链（可选：设置开关 + 共享 OCR 引擎可用） ----
    // T-047-UIA：管线内部 UIA-first、截图+OCR 兜底，source 随事件给出
    m_appliedOcrSnapshot = s.monitorOcrSnapshot();
    if (s.monitorOcrSnapshot() && m_services && m_services->ocr) {
        m_imeDetector = new ImeCommitDetector(this);
        m_ocrPipeline = new SnapshotOcrPipeline(m_services->ocr, this);
        connect(m_imeDetector, &ImeCommitDetector::commitDetected, m_ocrPipeline,
                &SnapshotOcrPipeline::onCommitDetected);
        connect(m_ocrPipeline, &SnapshotOcrPipeline::ocrTextReady, this,
                [this](const QString &app, const QString &title, const QString &text,
                       const QString &source) {
                    QJsonObject o = baseEvent(app, title);
                    o.insert("text", text);
                    o.insert("source", source); // "uia" / "ocr"
                    m_store->append(QStringLiteral("ocrtext"), o);
                });
    }

    // ---- 剪贴板流（T-047-CLIP：窗口消息监听，随监控开关装卸） ----
    m_clipboard = new ClipboardListener(m_store, this);
    if (!m_clipboard->start())
        qWarning("ActivityMonitorModule: 剪贴板监听启动失败（本流缺失）");
    // 外部剪贴板变化 → 文本镜像补一拍（粘贴进输入框无键入事件，靠此触发）
    connect(m_clipboard, &ClipboardListener::clipboardUpdated, m_mirror,
            &TextMirrorMonitor::onTypingActivity);

    // 鼠标按下 → 活跃度信号
    connect(m_mouseHook, &GlobalMouseHook::mousePressed, this, [this] {
        m_activity->noteClick();
    });

    // 1s 心跳：状态机活跃计数
    m_tick = new QTimer(this);
    connect(m_tick, &QTimer::timeout, this, [this] {
        m_activity->tick();
    });
    m_tick->start(1000);

    m_kbHook->start();
    m_tracker->start();
    m_mouseHook->start();
}

// 逆序拆除：先停心跳与钩子（不再产生事件）→ flush 残余缓冲 → 删对象 →
// 最后停 store（ClipboardListener 须先于 store 停止冲账，见下方 delete 顺序）
void ActivityMonitorModule::stopMonitor()
{
    if (!m_store)
        return;
    if (m_tick) {
        m_tick->stop();
        delete m_tick;
        m_tick = nullptr;
    }
    if (m_kbHook)
        m_kbHook->stop();
    if (m_tracker)
        m_tracker->stop();
    if (m_mouseHook)
        m_mouseHook->stop();
    if (m_activity)
        m_activity->flush(); // 残余时间线段落盘
    delete m_kbHook;
    delete m_tracker;
    delete m_mouseHook;
    delete m_activity;
    delete m_mirror;
    delete m_imeDetector;
    delete m_ocrPipeline;
    delete m_clipboard; // stop 内冲账挂起 repeat——须在 store 停止之前
    m_kbHook = nullptr;
    m_tracker = nullptr;
    m_mouseHook = nullptr;
    m_activity = nullptr;
    m_mirror = nullptr;
    m_imeDetector = nullptr;
    m_ocrPipeline = nullptr;
    m_clipboard = nullptr;
    m_store->stop();
    delete m_store;
    m_store = nullptr;
}

} // namespace poptools
