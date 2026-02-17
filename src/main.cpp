#include <QApplication>
#include <QCommandLineParser>

#include <KAboutData>
#include <KLocalizedString>
#include <KCrash>

#include "kf6/kf6mainwindow.h"
#include "qpilotsync_version.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    KLocalizedString::setApplicationDomain("qpilotsync");

    KAboutData aboutData(
        QStringLiteral("qpilotsync"),
        i18n("QPilotSync"),
        QStringLiteral(QPILOTSYNC_VERSION_STRING),
        i18n("Modern Palm Pilot synchronization for Linux"),
        KAboutLicense::GPL_V3,
        i18n("(c) 2024-2025 QPilotSync contributors"));
    aboutData.setOrganizationDomain("qpilotsync.org");
    aboutData.setDesktopFileName(QStringLiteral("org.qpilotsync.QPilotSync"));
    KAboutData::setApplicationData(aboutData);

    KCrash::initialize();

    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);
    parser.process(app);
    aboutData.processCommandLine(&parser);

    KF6MainWindow window;
    window.show();

    return app.exec();
}
