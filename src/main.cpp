#include <QApplication>
#include <QCommandLineParser>
#include <QDir>

#include <KAboutData>
#include <KLocalizedString>
#include <KCrash>

#include "kf6/kf6mainwindow.h"
#include "qpilotsync_version.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Add the lib/ directory next to the executable as a plugin search path.
    // This allows KPluginMetaData::findPlugins() to discover conduit plugins
    // when running from the build directory without installing.
    QDir appDir(QCoreApplication::applicationDirPath());
    QString localPluginDir = appDir.absoluteFilePath(QStringLiteral("lib"));
    if (QDir(localPluginDir).exists()) {
        QCoreApplication::addLibraryPath(localPluginDir);
    }

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

    auto *window = new KF6MainWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();

    return app.exec();
}
