#include "GoodyModule.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTimer>

#include "../AppServices.h"
#include "core/hotkey/GlobalHotKey.h"
#include "core/hotkey/HotKeyNames.h"
#include "core/settings/Settings.h"
#include "ui/dsh/DshWindow.h"
#include "ui/goody/GoodyBallLogic.h"
#include "ui/goody/GoodyFloatingBall.h"

namespace poptools {

namespace {

// 数据目录口径唯一（QStandardPaths::AppLocalDataLocation 进程内恒定）
QString goodyDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/goody");
}

// dsh 运行时定位（T-074）：源码树形态从 exe 目录向上找 tools/（node/dsh 由
// scripts/install_dsh.py 装入）；安装包形态的运行时随包分发在 M3 定案
QString toolsDir()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 4; ++i) {
        if (dir.exists(QStringLiteral("tools/node/current/node.exe")))
            return dir.absolutePath() + QStringLiteral("/tools");
        if (!dir.cdUp())
            break;
    }
    return QString();
}
} // namespace

// 仅作 QObject 接线中枢；子对象经父子关系随模块析构
GoodyModule::GoodyModule(QObject *parent)
    : QObject(parent)
{
}

void GoodyModule::init(AppServices &services)
{
    m_services = &services;

    // 呼出热键（注册失败由组合根统一气泡告知）
    const quint32 hkVk = Settings::instance().assistantHotKeyVk();
    const quint32 hkMods = Settings::instance().assistantHotKeyModifiers();
    services.registerHotKey(hkVk, hkMods, [this] {
        // 热键回调在 GlobalHotKey 原生事件过滤器栈中直发——GUI 操作延迟到
        // 事件循环（PinModule/CaptureModule singleShot 先例）
        QTimer::singleShot(0, QCoreApplication::instance(), [this] { openChat(); });
    });

    services.goodyOpenChat = [this] { openChat(); };

    // T-067：工作摘要查询槽（桌面日历消费）——路径即存在性，内容由消费方读。
    // 文件约定路径 goody/daily|monthly/，M2 起 dsh 定时任务经 MCP 桥写入续命
    services.goodyDailyDigestPath = [](int y, int m, int d) {
        const QDate date(y, m, d);
        if (!date.isValid())
            return QString();
        const QString p = goodyDir() + QStringLiteral("/daily/")
                          + date.toString(QStringLiteral("yyyy-MM-dd"))
                          + QStringLiteral(".md");
        return QFile::exists(p) ? p : QString();
    };
    services.goodyMonthlyDigestPath = [](int y, int m) {
        const QDate date(y, m, 1); // 日取 1 仅校验年月合法
        if (!date.isValid())
            return QString();
        const QString p = goodyDir() + QStringLiteral("/monthly/")
                          + date.toString(QStringLiteral("yyyy-MM"))
                          + QStringLiteral(".md");
        return QFile::exists(p) ? p : QString();
    };

    // T-074：dsh 常驻窗 + sidecar 常驻启动（PopTools 存续期后台运行，
    // 悬浮球/热键/设置页只唤起窗口不启停服务——用户 2026-11-21 拍板）
    ensureDshWindow();

    // T-050：桌面悬浮球（开关默认开；关=销毁，设置页即时重应用）
    applyBallSettings();

    connect(&services, &AppServices::settingsChanged, this, [this] { applyBallSettings(); });
}

// 退出前收口：能力槽撤销防悬垂（M3 常驻语义定案后 sidecar 由组合根统一关停）
void GoodyModule::shutdown()
{
    if (m_services)
        m_services->goodyOpenChat = nullptr;
}

// ---- T-074：dsh 常驻窗 ----

// 懒建（幂等）：init 即调——sidecar 常驻启动，悬浮球/热键/设置页唤起只是 show
void GoodyModule::ensureDshWindow()
{
    if (m_dshWindow)
        return;
    m_dshWindow = new DshWindow();
    const QString tools = toolsDir();
    const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    m_dshWindow->configure(
        tools + QStringLiteral("/node/current/node.exe"),
        tools + QStringLiteral("/dsh/node_modules/@deepseek-ai/dsh/lib/bin.js"),
        dataRoot + QStringLiteral("/dsh-home"),
        dataRoot + QStringLiteral("/dsh-agents-home"),
        Settings::instance().dshApiKey(),
        dataRoot + QStringLiteral("/WebView2"));
    // sidecar 工作态 → 悬浮球呼吸（Starting 呼吸 / Ready、Failed 停）
    connect(m_dshWindow, &DshWindow::workingChanged, this, [this](bool working) {
        m_working = working;
        emit workingChanged(working);
    });
    // 窗口隐藏回后台：悬浮球按开关回归
    connect(m_dshWindow, &DshWindow::windowHidden, this, [this] {
        if (m_ball && Settings::instance().assistantBallEnabled())
            m_ball->show();
    });
    connect(m_dshWindow, &QObject::destroyed, this, [this] { m_dshWindow = nullptr; });
}

// ---- T-050：桌面悬浮球 ----

// 懒建（幂等）：接线单击开窗、拖动位置持久化、工作呼吸光效与隐没同步
void GoodyModule::ensureBall()
{
    if (m_ball)
        return;
    m_ball = new GoodyFloatingBall();
    m_ball->applySavedPosition();
    connect(m_ball, &GoodyFloatingBall::clicked, this, [this] { openChat(); });
    connect(m_ball, &GoodyFloatingBall::positionChanged, this,
            [](const QPoint &pos) {
                Settings::instance().setAssistantBallPos(
                    goodyball::ballPosToString(pos));
            });
    connect(this, &GoodyModule::workingChanged, m_ball, &GoodyFloatingBall::setWorking);
    // 球建时若已在工作态，立即同步呼吸光效
    m_ball->setWorking(m_working);
    connect(m_ball, &QObject::destroyed, this, [this] { m_ball = nullptr; });
}

// 开关重应用（init 与 settingsChanged 共用）：开=建+显示（dsh 窗可见时
// 保持隐没）；关=销毁球。位置在球内 applySavedPosition 重钳制（换屏/改 ini 生效）
void GoodyModule::applyBallSettings()
{
    if (Settings::instance().assistantBallEnabled()) {
        ensureBall();
        m_ball->applySavedPosition();
        if (!m_dshWindow || !m_dshWindow->isVisible())
            m_ball->show();
        else
            m_ball->hide(); // dsh 窗开着：保持隐没
    } else if (m_ball) {
        m_ball->deleteLater(); // 关闭=销毁
        m_ball = nullptr;
    }
}

// 唯一唤起点：dsh 常驻窗前置/激活（服务已常驻，无启动等待）；悬浮球隐没，
// 关窗经 windowHidden 回归
void GoodyModule::openChat()
{
    ensureDshWindow();
    if (m_dshWindow->isVisible()) {
        m_dshWindow->raise();
        m_dshWindow->activateWindow();
        return;
    }
    m_dshWindow->openWindow();
    // 聊天时悬浮球隐没（关窗时经 windowHidden 回归）
    if (m_ball)
        m_ball->hide();
}

} // namespace poptools
