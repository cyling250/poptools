#include <QApplication>
#include <QIcon>

#include "app/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("PopTools");
    QApplication::setOrganizationName("PopTools");
    // 应用窗口/任务栏图标（托盘图标由 MainWindow 单独设置）
    QApplication::setWindowIcon(QIcon(":/app_icon.svg"));

    poptools::MainWindow w;
    // MainWindow 为纯 QObject（托盘 + 热键），无需显示窗口
    return app.exec();
}
