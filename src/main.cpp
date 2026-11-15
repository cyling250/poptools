#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QStyleFactory>
#include <QStyleHints>

#include "app/CrashReporter.h"
#include "app/MainWindow.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
    // 崩溃自诊最先安装（早于 QApplication）：未处理异常/qFatal →
    // exe 旁 crash/ 落 MiniDump + 符号化栈日志
    poptools::CrashReporter::install();

    QApplication app(argc, argv);
    QApplication::setApplicationName("PopTools");
    QApplication::setOrganizationName("PopTools");
    // 托盘常驻应用：进程唯一退出入口是托盘菜单「退出」。
    // 默认的 quitOnLastWindowClosed=true 会在「设置/关于」这类无主对话框
    // 关闭时静默结束整个进程，必须显式关闭
    QApplication::setQuitOnLastWindowClosed(false);
    // 应用窗口/任务栏图标（托盘图标由 MainWindow 单独设置）
    QApplication::setWindowIcon(QIcon(":/app_icon.svg"));

    // 统一使用 Fusion 风格：Win10 的 windowsvista 原生风格与深色调色板/QSS
    // 组合存在文字颜色不生效等非确定渲染（故障机实测：深色下源文本呈灰色），
    // Fusion 完全由 Qt/QSS 接管，跨 Windows 版本表现一致
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    // 主题强制覆盖（验证/演示用）：POPTOOLS_COLOR_SCHEME=light|dark；
    // 未设置时跟随系统明暗
    const QString forcedScheme = qEnvironmentVariable("POPTOOLS_COLOR_SCHEME").toLower();
    if (forcedScheme == QLatin1String("light") || forcedScheme == QLatin1String("dark"))
        QApplication::styleHints()->setColorScheme(
            forcedScheme == QLatin1String("dark") ? Qt::ColorScheme::Dark
                                                  : Qt::ColorScheme::Light);

#ifdef Q_OS_WIN
    // 单实例保护：全局热键是系统独占资源，双实例时后启动者只会静默
    // 抢不到热键；互斥量随进程存活，无需显式释放
    const HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"PopTools.SingleInstance");
    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        QMessageBox::information(nullptr, QStringLiteral("PopTools"),
                                 QStringLiteral("PopTools 已在运行（见系统托盘图标）。"));
        return 0;
    }
#endif

    poptools::MainWindow w;
    // MainWindow 为纯 QObject（托盘 + 热键），无需显示窗口
    return app.exec();
}
