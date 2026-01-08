#include <QApplication>
#include "app/mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("QPilotSync");
    app.setOrganizationName("QPilotSync");

    MainWindow window;
    window.show();

    return app.exec();
}
