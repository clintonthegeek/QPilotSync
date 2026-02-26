#include "installconduit.h"
#include "installview.h"
#include "sync/synctypes.h"
#include "sync/conduit.h"
#include "palm/kpilotdevicelink.h"

#include <KPluginFactory>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

extern "C" {
#include <pi-file.h>
}

K_PLUGIN_FACTORY_WITH_JSON(InstallConduitFactory, "install-conduit.json",
                           registerPlugin<InstallConduit>();)

InstallConduit::InstallConduit(QObject *parent)
    : QObject(parent)
{
}

QIcon InstallConduit::icon() const
{
    return QIcon::fromTheme(QStringLiteral("document-import"));
}

QString InstallConduit::description() const
{
    return QStringLiteral("Installs .prc and .pdb files to Palm devices");
}

QIcon InstallConduit::viewIcon() const
{
    return QIcon::fromTheme(QStringLiteral("document-import"));
}

void InstallConduit::setInstallFolder(const QString &path)
{
    m_installFolder = path;
    if (m_view) {
        m_view->setInstallFolder(path);
    }
}

bool InstallConduit::canSync(const Sync::SyncContext *context) const
{
    return context && context->deviceLink;
}

bool InstallConduit::shouldRun(const Sync::SyncContext *context) const
{
    if (!context || context->syncFolderPath.isEmpty()) {
        return false;
    }
    QString folder = QDir(context->syncFolderPath).filePath(QStringLiteral("install"));
    return !pendingFiles(folder).isEmpty();
}

QStringList InstallConduit::pendingFiles(const QString &installFolder) const
{
    QStringList files;
    QDir dir(installFolder);
    if (!dir.exists()) {
        return files;
    }

    const QStringList filters{
        QStringLiteral("*.prc"), QStringLiteral("*.pdb"),
        QStringLiteral("*.PRC"), QStringLiteral("*.PDB")
    };

    const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files | QDir::Readable);
    for (const QFileInfo &info : entries) {
        files << info.absoluteFilePath();
    }
    return files;
}

QStringList InstallConduit::installedFiles(const QString &installFolder) const
{
    QStringList files;
    QDir dir(QDir(installFolder).filePath(QStringLiteral("installed")));
    if (!dir.exists()) {
        return files;
    }

    const QStringList filters{
        QStringLiteral("*.prc"), QStringLiteral("*.pdb"),
        QStringLiteral("*.PRC"), QStringLiteral("*.PDB")
    };

    const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files | QDir::Readable);
    for (const QFileInfo &info : entries) {
        files << info.absoluteFilePath();
    }
    return files;
}

Sync::SyncResult InstallConduit::sync(Sync::SyncContext *context)
{
    Sync::SyncResult result;
    result.startTime = QDateTime::currentDateTime();
    result.success = true;

    if (!context || !context->deviceLink || context->syncFolderPath.isEmpty()) {
        result.success = false;
        result.errorMessage = QStringLiteral("No device or sync folder");
        result.endTime = QDateTime::currentDateTime();
        return result;
    }

    QString folder = QDir(context->syncFolderPath).filePath(QStringLiteral("install"));
    ensureFoldersExist(folder);

    QStringList files = pendingFiles(folder);
    if (files.isEmpty()) {
        result.endTime = QDateTime::currentDateTime();
        return result;
    }

    int socket = context->deviceLink->socketDescriptor();

    for (const QString &filePath : files) {
        if (context->cancelled) {
            break;
        }

        QFileInfo fi(filePath);
        if (installFile(filePath, socket)) {
            moveToInstalled(filePath, folder);
            result.palmStats.created++;
            qDebug() << "[InstallConduit] Installed:" << fi.fileName();
        } else {
            result.palmStats.errors++;
            qDebug() << "[InstallConduit] Failed:" << fi.fileName();
        }
    }

    result.endTime = QDateTime::currentDateTime();
    if (result.palmStats.errors > 0 && result.palmStats.created == 0) {
        result.success = false;
        result.errorMessage = QStringLiteral("All file installations failed");
    }

    return result;
}

bool InstallConduit::installFile(const QString &filePath, int socket)
{
    pi_file_t *pf = pi_file_open(filePath.toLocal8Bit().constData());
    if (!pf) {
        return false;
    }

    int rc = pi_file_install(pf, socket, 0, nullptr);
    pi_file_close(pf);

    return rc >= 0;
}

bool InstallConduit::moveToInstalled(const QString &filePath, const QString &installFolder)
{
    QDir installDir(installFolder);
    QString installedPath = installDir.filePath(QStringLiteral("installed"));

    if (!QDir(installedPath).exists()) {
        installDir.mkpath(QStringLiteral("installed"));
    }

    QFileInfo info(filePath);
    QString destPath = QDir(installedPath).filePath(info.fileName());

    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }

    return QFile::rename(filePath, destPath);
}

void InstallConduit::ensureFoldersExist(const QString &installFolder)
{
    QDir dir(installFolder);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    QString installedPath = dir.filePath(QStringLiteral("installed"));
    if (!QDir(installedPath).exists()) {
        dir.mkpath(QStringLiteral("installed"));
    }
}

QWidget *InstallConduit::createView(QWidget *parent)
{
    m_view = new InstallView(parent);
    if (!m_installFolder.isEmpty()) {
        m_view->setInstallFolder(m_installFolder);
    }
    return m_view;
}

#include "installconduit.moc"
