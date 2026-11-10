#include "ActivityMonitorModule.h"

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>

#include "../AppServices.h"
#include "core/hotkey/GlobalMouseHook.h"
#include "core/monitor/ActivityStore.h"
#include "core/monitor/ForegroundTracker.h"
#include "core/monitor/GlobalKeyboardHook.h"
#include "core/monitor/ImeCommitDetector.h"
#include "core/monitor/InputActivityMonitor.h"
#include "core/monitor/KeyboardTextComposer.h"
#include "core/monitor/PasswordFieldGuard.h"
#include "core/monitor/SnapshotOcrPipeline.h"
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
    // 托盘文案（enabled=false 时只改文案不建任何运行期对象）
    QTimer::singleShot(0, QCoreApplication::instance(),
                       [this] { applyRunningState(); });
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
    if (m_trayAction)
        m_trayAction->setText(want ? QStringLiteral("活动监控：运行中（点击暂停）")
                                   : QStringLiteral("活动监控：已暂停（点击开启）"));
}

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

void ActivityMonitorModule::startMonitor()
{
    if (m_store)
        return;
    const Settings &s = Settings::instance();

    m_store = new ActivityStore(QString(), this);
    m_store->start();

    m_composer = new KeyboardTextComposer(this);
    m_composer->setMaskPasswords(s.monitorMaskPasswords());

    m_activity = new InputActivityMonitor(this);
    m_activity->setIdleThresholdSec(s.monitorIdleThresholdSec());

    m_kbHook = new GlobalKeyboardHook(this);
    m_tracker = new ForegroundTracker(this);
    m_mouseHook = new GlobalMouseHook(this);

    // 键盘事件：down 才记键入；打码判定每键一次（GUITHREADINFO，µs 级）
    connect(m_kbHook, &GlobalKeyboardHook::keyEvent, this,
            [this](int vk, int scan, bool ext, bool down) {
                if (down) {
                    m_composer->setMasked(Settings::instance().monitorMaskPasswords()
                                              && PasswordFieldGuard::isPasswordFocused());
                    m_composer->onKeyEvent(vk, scan, ext, down);
                    m_activity->noteKeydown();
                    if (m_imeDetector)
                        m_imeDetector->onKeyEvent(vk, down);
                } else {
                    m_composer->onKeyEvent(vk, scan, ext, down); // 修饰键松开也要喂状态机
                }
            });

    // 键入文本 → keys.jsonl
    connect(m_composer, &KeyboardTextComposer::textReady, this,
            [this](const QString &app, const QString &title, const QString &text, bool masked) {
                QJsonObject o = baseEvent(app, title);
                o.insert("text", text);
                if (masked)
                    o.insert("masked", true);
                m_store->append(QStringLiteral("keys"), o);
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

    // 前台窗口变化 → 全链换上下文（状态机 / 文本 / 上屏检测 / OCR 基线）
    connect(m_tracker, &ForegroundTracker::foregroundChanged, this,
            [this](quintptr hwnd, const QString &app, const QString &title) {
                m_activity->setWindowContext(hwnd, app, title);
                m_composer->setWindowContext(app, title);
                if (m_imeDetector)
                    m_imeDetector->reset();
                if (m_ocrPipeline)
                    m_ocrPipeline->setWindowContext(hwnd, app, title);
            });

    // ---- 中文上屏 OCR 快照链（可选：设置开关 + 共享 OCR 引擎可用） ----
    if (s.monitorOcrSnapshot() && m_services && m_services->ocr) {
        m_imeDetector = new ImeCommitDetector(this);
        m_ocrPipeline = new SnapshotOcrPipeline(m_services->ocr, this);
        connect(m_imeDetector, &ImeCommitDetector::commitDetected, m_ocrPipeline,
                &SnapshotOcrPipeline::onCommitDetected);
        connect(m_ocrPipeline, &SnapshotOcrPipeline::ocrTextReady, this,
                [this](const QString &app, const QString &title, const QString &text) {
                    QJsonObject o = baseEvent(app, title);
                    o.insert("text", text);
                    o.insert("source", QStringLiteral("ocr"));
                    m_store->append(QStringLiteral("ocrtext"), o);
                });
    }

    // 鼠标按下 → 活跃度信号
    connect(m_mouseHook, &GlobalMouseHook::mousePressed, this, [this] {
        m_activity->noteClick();
    });

    // 1s 心跳：状态机活跃计数 + 文本空闲冲刷
    m_tick = new QTimer(this);
    connect(m_tick, &QTimer::timeout, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        m_activity->tick();
        m_composer->onIdleTick(now);
    });
    m_tick->start(1000);

    m_kbHook->start();
    m_tracker->start();
    m_mouseHook->start();
}

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
    if (m_composer)
        m_composer->flush(); // 残余键入文本落盘
    if (m_activity)
        m_activity->flush(); // 残余时间线段落盘
    delete m_kbHook;
    delete m_tracker;
    delete m_mouseHook;
    delete m_composer;
    delete m_activity;
    delete m_imeDetector;
    delete m_ocrPipeline;
    m_kbHook = nullptr;
    m_tracker = nullptr;
    m_mouseHook = nullptr;
    m_composer = nullptr;
    m_activity = nullptr;
    m_imeDetector = nullptr;
    m_ocrPipeline = nullptr;
    m_store->stop();
    delete m_store;
    m_store = nullptr;
}

} // namespace poptools
