#include "app/AppController.h"
#include "app/MainWindow.h"
#include "app/Settings.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("AdoLoop"));
    QCoreApplication::setOrganizationName(QStringLiteral("AdoLoop"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    adoloop::Settings::initialize();

    adoloop::MainWindow win;
    win.show();

    adoloop::AppController ctrl(&win);
    ctrl.initialize();

    return app.exec();
}
