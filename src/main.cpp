#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("MySnipaste");
    QApplication::setOrganizationName("MySnipaste");

    MainWindow w;
    // MainWindow 为纯 QObject（托盘 + 热键），无需显示窗口
    return app.exec();
}
