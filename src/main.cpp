#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("动力电池及BMS管理系统"));
    MainWindow window;
    window.show();
    return app.exec();
}
