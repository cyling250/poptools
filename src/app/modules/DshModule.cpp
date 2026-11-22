#include "DshModule.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFileSystemWatcher>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include "../AppServices.h"
#include "core/hotkey/GlobalHotKey.h"
#include "core/hotkey/HotKeyNames.h"
#include "core/settings/Settings.h"
#include "ui/dsh/DshBallLogic.h"
#include "ui/dsh/DshFloatingBall.h"

namespace poptools {

namespace {

// 数据目录口径唯一（QStandardPaths::AppLocalDataLocation 进程内恒定）
QString dataRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QString dshDir()
{
    return dataRoot() + QStringLiteral("/dsh");
}

// dsh 官方桌面端（T-078）：Electron 应用自带 host/更新/账号，主界面即它本体。
// electron-builder NSIS 默认 per-user 安装位；未安装返回空（openChat 提示）
QString dshDesktopExe()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/Programs/DeepSeek Harness/DeepSeek Harness.exe");
}

// 桌面端 host 家（shared profile runner 无 DSH_HOME 注入时用默认 ~/.dsh）
QString desktopHome()
{
    return QDir::home().filePath(QStringLiteral(".dsh"));
}

QString desktopPatchPath()
{
    return desktopHome() + QStringLiteral("/profiles/desktop/cordis.patch.yml");
}

} // namespace

// 仅作 QObject 接线中枢；子对象经父子关系随模块析构
DshModule::DshModule(QObject *parent)
    : QObject(parent)
{
}

void DshModule::init(AppServices &services)
{
    m_services = &services;

    // 呼出热键（注册失败由组合根统一气泡告知）
    const quint32 hkVk = Settings::instance().dshHotKeyVk();
    const quint32 hkMods = Settings::instance().dshHotKeyModifiers();
    services.registerHotKey(hkVk, hkMods, [this] {
        // 热键回调在 GlobalHotKey 原生事件过滤器栈中直发——GUI 操作延迟到
        // 事件循环（PinModule/CaptureModule singleShot 先例）
        QTimer::singleShot(0, QCoreApplication::instance(), [this] { openChat(); });
    });

    services.dshOpenChat = [this] { openChat(); };

    // T-067：工作摘要查询槽（桌面日历消费）——路径即存在性，内容由消费方读。
    // 文件约定路径 dsh/daily|monthly/，M2 起 dsh 定时任务经 MCP 桥写入续命
    services.dshDailyDigestPath = [](int y, int m, int d) {
        const QDate date(y, m, d);
        if (!date.isValid())
            return QString();
        const QString p = dshDir() + QStringLiteral("/daily/")
                          + date.toString(QStringLiteral("yyyy-MM-dd"))
                          + QStringLiteral(".md");
        return QFile::exists(p) ? p : QString();
    };
    services.dshMonthlyDigestPath = [](int y, int m) {
        const QDate date(y, m, 1); // 日取 1 仅校验年月合法
        if (!date.isValid())
            return QString();
        const QString p = dshDir() + QStringLiteral("/monthly/")
                          + date.toString(QStringLiteral("yyyy-MM"))
                          + QStringLiteral(".md");
        return QFile::exists(p) ? p : QString();
    };

    // T-078 M1：桌面端集成装配（patch/迁移） + 心跳监听 + 悬浮球
    ensureDesktopIntegration();
    ensureHeartbeatWatcher();
    applyBallSettings();

    connect(&services, &AppServices::settingsChanged, this, [this] { applyBallSettings(); });
}

// 退出前收口：能力槽撤销防悬垂
void DshModule::shutdown()
{
    if (m_services)
        m_services->dshOpenChat = nullptr;
}

// ---- T-078 M1：桌面端集成装配（init 一次性，全部幂等） ----

void DshModule::ensureDesktopIntegration()
{
    const QString root = dataRoot();

    // 1) 旧 goody 数据目录迁移（T-078 改名：日报/月报随目录更名，幂等）
    const QDir rootDir(root); // 读判定用
    if (rootDir.exists(QStringLiteral("goody")) && !rootDir.exists(QStringLiteral("dsh"))) {
        if (QDir(root).rename(QStringLiteral("goody"), QStringLiteral("dsh")))
            qInfo("DshModule: 旧 goody 数据目录已迁移为 dsh");
        else
            qWarning("DshModule: goody→dsh 目录迁移失败（日报链接沿用旧路径）");
    }

    // 2) MCP patch 幂等写入桌面端 profile（mcp-manager 条目，command 随
    //    当前 exe 形态更新：开发树 build/Release vs 安装形态 {app}）
    const QString exe = QCoreApplication::applicationDirPath()
                        + QStringLiteral("/poptools_mcp_server.exe");
    const QString patchPath = desktopPatchPath();
    QDir().mkpath(QFileInfo(patchPath).absolutePath());
    QFile patchFile(patchPath);
    const QString entryHeader = QStringLiteral("- id: mcp-manager");
    if (!patchFile.exists()) {
        // 全新 patch（官方升级保留用户 patch；首次装配兜底）
        const QString fresh =
            QStringLiteral(
                "# PopTools 桌面能力桥（T-074 M2 能力桥）：把桌面能力以 stdio MCP"
                "\n# 服务器挂进 dsh（由 PopTools 后台服务维护，随主程序更新）。\n")
            + QStringLiteral("- id: mcp-manager\n  config:\n    servers:\n"
                             "      poptools:\n        transport: stdio\n"
                             "        command: \"%1\"\n        args: []\n"
                             "        env: {}\n        cwd: \"\"\n        url: \"\"\n"
                             "        headers: {}\n        enabled: true\n"
                             "        startupTimeoutMs: 60000\n"
                             "        toolLoading: eager\n").arg(exe);
        if (patchFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            patchFile.write(fresh.toUtf8());
            qInfo("DshModule: 桌面端 MCP patch 已写入 %s", qUtf8Printable(patchPath));
        }
    } else if (patchFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString existing = QString::fromUtf8(patchFile.readAll());
        patchFile.close();
        if (!existing.contains(entryHeader)) {
            // 用户 patch 已有内容但无 MCP 条目 → 追加（保持用户自定义不动）
            if (patchFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
                patchFile.write(QStringLiteral("\n%1").arg(entryHeader).toUtf8());
                patchFile.write(QStringLiteral("\n  config:\n    servers:\n"
                                               "      poptools:\n        transport: stdio\n"
                                               "        command: \"%1\"\n        args: []\n"
                                               "        env: {}\n        cwd: \"\"\n"
                                               "        url: \"\"\n        headers: {}\n"
                                               "        enabled: true\n"
                                               "        startupTimeoutMs: 60000\n"
                                               "        toolLoading: eager\n").arg(exe).toUtf8());
                qInfo("DshModule: 桌面端 MCP patch 追加 mcp-manager 条目");
            }
        } else if (!existing.contains(exe)) {
            // 条目在但 exe 路径过时（安装形态/目录变化）→ 更新 command 行
            QString updated = existing;
            const QRegularExpression re(
                QStringLiteral("(command: \")([^\"]*poptools_mcp_server\\.exe)(\")"));
            updated.replace(re, QStringLiteral("\\1%1\\3").arg(exe));
            if (updated != existing
                && patchFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                patchFile.write(updated.toUtf8());
                qInfo("DshModule: 桌面端 MCP patch command 已更新为当前 exe");
            }
        }
    }

    // 3) AGENTS.md 人格基线迁移（旧 sidecar 家 → 桌面端家；目标存在不覆盖）
    const QString oldAgents = root + QStringLiteral("/dsh-home/AGENTS.md");
    const QString newAgents = desktopHome() + QStringLiteral("/AGENTS.md");
    if (QFileInfo::exists(oldAgents) && !QFileInfo::exists(newAgents)) {
        QFile src(oldAgents);
        if (src.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString text = QString::fromUtf8(src.readAll());
            text.replace(QStringLiteral("Goody 管家"), QStringLiteral("DeepSeek Harness"));
            text.replace(QStringLiteral("小管家 Goody"),
                         QStringLiteral("小管家（DeepSeek Harness）"));
            QDir().mkpath(desktopHome());
            QFile dst(newAgents);
            if (dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                dst.write(text.toUtf8());
                qInfo("DshModule: AGENTS.md 人格基线已迁移到桌面端家");
            }
        }
    }
}

// MCP 心跳 → 悬浮球呼吸（T-074 M2.5）：dsh agent 每次调用 PopTools 工具，
// poptools_mcp_server 触碰 mcp.heartbeat；10s 无心跳回静默。桌面端形态下
// 该链路不变（mcp_server 数据目录与 PopTools 同源）
void DshModule::ensureHeartbeatWatcher()
{
    const QString root = dataRoot();
    auto *hbWatcher = new QFileSystemWatcher(this);
    if (QDir(root).exists())
        hbWatcher->addPath(root);
    auto *revert = new QTimer(this);
    revert->setSingleShot(true);
    revert->setInterval(10 * 1000);
    connect(revert, &QTimer::timeout, this, [this] {
        m_working = false;
        emit workingChanged(false);
    });
    connect(hbWatcher, &QFileSystemWatcher::directoryChanged, this,
            [this, revert, root](const QString &) {
                if (!QFileInfo::exists(root + QStringLiteral("/mcp.heartbeat")))
                    return;
                QFile::remove(root + QStringLiteral("/mcp.heartbeat"));
                if (!m_working) {
                    m_working = true;
                    emit workingChanged(true);
                }
                revert->start();
            });
}

// ---- 桌面悬浮球 ----

// 懒建（幂等）：接线单击唤起桌面端、拖动位置持久化、工作呼吸光效
void DshModule::ensureBall()
{
    if (m_ball)
        return;
    m_ball = new DshFloatingBall();
    m_ball->applySavedPosition();
    connect(m_ball, &DshFloatingBall::clicked, this, [this] { openChat(); });
    connect(m_ball, &DshFloatingBall::positionChanged, this,
            [](const QPoint &pos) {
                Settings::instance().setDshBallPos(
                    dshball::ballPosToString(pos));
            });
    connect(this, &DshModule::workingChanged, m_ball, &DshFloatingBall::setWorking);
    // 球建时若已在工作态，立即同步呼吸光效
    m_ball->setWorking(m_working);
    connect(m_ball, &QObject::destroyed, this, [this] { m_ball = nullptr; });
}

// 开关重应用（init 与 settingsChanged 共用）：开=建+显示；关=销毁球。
// 桌面端是独立应用（PopTools 无窗口可见性耦合），球常显
void DshModule::applyBallSettings()
{
    if (Settings::instance().dshBallEnabled()) {
        ensureBall();
        m_ball->applySavedPosition();
        m_ball->show();
    } else if (m_ball) {
        m_ball->deleteLater(); // 关闭=销毁
        m_ball = nullptr;
    }
}

// 唯一唤起点：官方桌面端启动/前置（T-078 M1）。桌面端自带 single-instance
// 锁（第二实例把 second-instance 信号发给首实例后退出，首实例聚焦主窗），
// 已在跑时再启动一次即等价于「前置」
void DshModule::openChat()
{
    const QString exe = dshDesktopExe();
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        QMessageBox::information(
            nullptr, QStringLiteral("DeepSeek Harness"),
            QStringLiteral("未检测到 DeepSeek Harness 桌面端。\n\n"
                           "重新安装或更新 PopTools 时会自动下载安装。"));
        return;
    }
    if (!QProcess::startDetached(exe, {}))
        QMessageBox::warning(nullptr, QStringLiteral("DeepSeek Harness"),
                             QStringLiteral("桌面端启动失败，请手动运行：\n%1").arg(exe));
}

} // namespace poptools
