// ============================================================
// dsh_poc：T-074 M1 端到端验证窗（独立可执行，不入正式产品）。
//
// 验证链：WebView2 嵌入（先本地 data: 页）→ DshRuntime sidecar 拉起
// → stdout 解析启动 URL（含 ?token=）→ WebView2 导航 → dsh 原版
// 界面呈现。窗口布局：顶部工具条（启动/本地页/停止 + 地址行）+ WebView2
// 宿主区 + 底部日志。tools 定位：从 exe 目录向上找 tools/node/current。
// ============================================================

#include "ui/dsh/DshRuntime.h"
#include "ui/dsh/WebView2Host.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTime>
#include <QVBoxLayout>

using poptools::DshRuntime;
using poptools::WebView2Host;

namespace {

// 从 exe 目录向上找工程根（tools/node/current/node.exe 存在的层级）
QString findProjectRoot()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 4; ++i) {
        if (dir.exists("tools/node/current/node.exe"))
            return dir.absolutePath();
        if (!dir.cdUp())
            break;
    }
    return QString();
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("dsh_poc");
    QApplication::setOrganizationName("PopTools");

    QWidget window;
    window.setWindowTitle(QStringLiteral("dsh PoC（T-074 M1）"));
    window.resize(1180, 780);

    auto *rootLay = new QVBoxLayout(&window);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(0);

    // ---- 工具条 ----
    auto *toolbar = new QWidget(&window);
    auto *tbLay = new QHBoxLayout(toolbar);
    tbLay->setContentsMargins(8, 6, 8, 6);
    tbLay->setSpacing(8);
    auto *startBtn = new QPushButton(QStringLiteral("启动 dsh sidecar"), toolbar);
    auto *stopBtn = new QPushButton(QStringLiteral("停止"), toolbar);
    auto *localPageBtn = new QPushButton(QStringLiteral("本地页测试"), toolbar);
    auto *urlLabel = new QLineEdit(toolbar);
    urlLabel->setReadOnly(true);
    urlLabel->setPlaceholderText(QStringLiteral("等待 dsh web 启动 URL…"));
    tbLay->addWidget(startBtn);
    tbLay->addWidget(stopBtn);
    tbLay->addWidget(localPageBtn);
    tbLay->addWidget(urlLabel, 1);
    rootLay->addWidget(toolbar);

    // ---- WebView2 宿主区 ----
    auto *web = new WebView2Host(&window);
    web->setMinimumHeight(300);
    rootLay->addWidget(web, 1);

    // ---- 日志区 ----
    auto *log = new QPlainTextEdit(&window);
    log->setReadOnly(true);
    log->setMaximumHeight(160);
    log->setPlaceholderText(QStringLiteral("sidecar 输出与事件日志"));
    rootLay->addWidget(log);

    // ---- 运行时与路径 ----
    DshRuntime dsh(&window);
    const QString root = findProjectRoot();
    const QString nodeExe = root + "/tools/node/current/node.exe";
    const QString dshBin = root + "/tools/dsh/node_modules/@deepseek-ai/dsh/lib/bin.js";
    const QString dataRoot =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    // DeepSeek API Key：本机 PopTools.ini [dsh]（与主程序同款 exe 目录 ini；
    // 不入仓库，PoC 与主程序共享该配置键）
    const QSettings localIni(QCoreApplication::applicationDirPath() + "/PopTools.ini",
                             QSettings::IniFormat);
    const QString apiKey = localIni.value(QStringLiteral("dsh/apiKey")).toString();
    dsh.configure(nodeExe, dshBin, dataRoot + "/dsh-home", dataRoot + "/dsh-agents-home", apiKey);
    web->setUserDataDir(dataRoot + "/WebView2PoC");

    const auto logLine = [log](const QString &text) {
        log->appendPlainText(QTime::currentTime().toString("HH:mm:ss ") + text);
    };

    if (root.isEmpty()) {
        logLine(QStringLiteral("未定位到工程 tools/（从 %1 向上找）")
                    .arg(QCoreApplication::applicationDirPath()));
    } else {
        logLine(QStringLiteral("node: %1（存在=%2）")
                    .arg(nodeExe, QFile::exists(nodeExe) ? "yes" : "NO"));
        logLine(QStringLiteral("dsh bin: %1（存在=%2）")
                    .arg(dshBin, QFile::exists(dshBin) ? "yes" : "NO"));
        logLine(QStringLiteral("DEEPSEEK_API_KEY：%1")
                    .arg(apiKey.isEmpty() ? "未配置（PopTools.ini [dsh] apiKey）"
                                          : QStringLiteral("已配置（%1…）").arg(apiKey.left(6))));
    }

    // ---- 接线 ----
    QObject::connect(startBtn, &QPushButton::clicked, &dsh, &DshRuntime::start);
    QObject::connect(stopBtn, &QPushButton::clicked, &dsh, &DshRuntime::stop);
    // 本地页：不经 dsh 单独验证 WebView2 嵌入（嵌入层好坏与 sidecar 无关）
    QObject::connect(localPageBtn, &QPushButton::clicked, web, [web] {
        web->navigate(QStringLiteral(
            "data:text/html;charset=utf-8,<body style=\"font-family:Segoe UI;"
            "display:flex;align-items:center;justify-content:center;height:90vh\">"
            "<div style=\"text-align:center\"><h1>WebView2 PoC ✓</h1>"
            "<p style=\"color:#555\">Chromium 宿主嵌入正常，可进入 dsh 加载环节</p></div></body>"));
    });
    QObject::connect(&dsh, &DshRuntime::webUrlReady, web, [web, urlLabel, logLine](const QString &url) {
        urlLabel->setText(url);
        logLine(QStringLiteral("启动 URL 就绪，导航 WebView2…"));
        web->navigate(url);
    });
    QObject::connect(&dsh, &DshRuntime::outputLine, &window, [logLine](const QString &line) {
        logLine(QStringLiteral("[dsh] %1").arg(line));
    });
    QObject::connect(&dsh, &DshRuntime::failed, &window, [logLine, urlLabel](const QString &reason) {
        urlLabel->setText(reason);
        logLine(QStringLiteral("失败：%1").arg(reason));
    });
    QObject::connect(&dsh, &DshRuntime::stateChanged, &window,
                     [logLine](DshRuntime::State state) {
                         switch (state) {
                         case DshRuntime::State::Starting: logLine("状态：Starting"); break;
                         case DshRuntime::State::Ready: logLine("状态：Ready"); break;
                         case DshRuntime::State::Stopping: logLine("状态：Stopping"); break;
                         case DshRuntime::State::Idle: logLine("状态：Idle"); break;
                         case DshRuntime::State::Failed: logLine("状态：Failed"); break;
                         }
                     });
    QObject::connect(web, &WebView2Host::initFailed, &window, [logLine](const QString &reason) {
        logLine(QStringLiteral("WebView2 初始化失败：%1").arg(reason));
    });
    QObject::connect(web, &WebView2Host::navigationCompleted, &window,
                     [logLine](bool ok, const QString &url) {
                         logLine(QStringLiteral("导航%1：%2").arg(ok ? "完成" : "失败", url));
                     });

    window.show();
    return QApplication::exec();
}
