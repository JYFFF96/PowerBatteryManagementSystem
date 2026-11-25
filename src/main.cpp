#include "dashboardwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("动力电池及管理系统调试软件"));
    DashboardWindow window;
    window.show();
    return app.exec();
}
