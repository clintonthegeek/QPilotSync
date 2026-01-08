#include "installconduit.h"

#include <QDir>
#include <QFileInfo>
#include <QDebug>

extern "C" {
#include <pi-file.h>
}

namespace Sync {

InstallConduit::InstallConduit(QObject *parent)
    : QObject(parent)
{
}

void InstallConduit::setInstallFolder(const QString &path)
{
    m_installFolder = path;
    ensureFoldersExist();
}

QStringList InstallConduit::pendingFiles() const
{
    QStringList files;

    if (m_installFolder.isEmpty()) {
        return files;
    }

    QDir dir(m_installFolder);
    if (!dir.exists()) {
        return files;
    }

    // Look for .prc and .pdb files
    QStringList filters;
    filters << "*.prc" << "*.pdb" << "*.PRC" << "*.PDB";

    QFileInfoList entries = dir.entryInfoList(filters, QDir::Files | QDir::Readable);
    for (const QFileInfo &info : entries) {
        files << info.absoluteFilePath();
    }

    return files;
}

QList<InstallResult> InstallConduit::installAll(int socket)
{
    QList<InstallResult> results;
    QStringList files = pendingFiles();

    if (files.isEmpty()) {
        emit logMessage("No files to install");
        return results;
    }

    emit logMessage(QString("Found %1 file(s) to install").arg(files.size()));

    int current = 0;
    for (const QString &filePath : files) {
        current++;
        QFileInfo info(filePath);
        emit progressUpdated(current, files.size(), info.fileName());

        InstallResult result = installFile(filePath, socket);
        results.append(result);

        emit fileInstalled(result.fileName, result.success);

        if (result.success) {
            emit logMessage(QString("Installed: %1").arg(result.fileName));

            // Move or delete the installed file
            if (m_keepInstalledFiles) {
                if (!moveToInstalled(filePath)) {
                    emit logMessage(QString("Warning: Could not move %1 to installed folder")
                                    .arg(result.fileName));
                }
            } else {
                QFile::remove(filePath);
            }
        } else {
            emit errorOccurred(QString("Failed to install %1: %2")
                               .arg(result.fileName)
                               .arg(result.errorMessage));
        }
    }

    // Summary
    int successCount = 0;
    for (const InstallResult &r : results) {
        if (r.success) successCount++;
    }

    emit logMessage(QString("Install complete: %1 of %2 files installed successfully")
                    .arg(successCount).arg(results.size()));

    return results;
}

InstallResult InstallConduit::installFile(const QString &filePath, int socket)
{
    InstallResult result;
    QFileInfo info(filePath);
    result.fileName = info.fileName();

    // Validate file exists
    if (!info.exists()) {
        result.success = false;
        result.errorMessage = "File not found";
        return result;
    }

    // Validate file extension
    QString ext = info.suffix().toLower();
    if (ext != "prc" && ext != "pdb") {
        result.success = false;
        result.errorMessage = "Not a Palm database file (.prc or .pdb)";
        return result;
    }

    // Open the file with pilot-link
    pi_file_t *pf = pi_file_open(filePath.toLocal8Bit().constData());
    if (!pf) {
        result.success = false;
        result.errorMessage = "Could not open file (invalid Palm database format?)";
        return result;
    }

    // Get database info for logging
    struct DBInfo dbInfo;
    pi_file_get_info(pf, &dbInfo);

    emit logMessage(QString("Installing database: %1 (type: %2, creator: %3)")
                    .arg(QString::fromLatin1(dbInfo.name))
                    .arg(QString::number(dbInfo.type, 16))
                    .arg(QString::number(dbInfo.creator, 16)));

    // Install to Palm (card 0 = internal storage)
    int rc = pi_file_install(pf, socket, 0, nullptr);

    pi_file_close(pf);

    if (rc < 0) {
        result.success = false;
        result.errorMessage = QString("pilot-link error code: %1").arg(rc);
        return result;
    }

    result.success = true;
    return result;
}

bool InstallConduit::moveToInstalled(const QString &filePath)
{
    if (m_installFolder.isEmpty()) {
        return false;
    }

    QDir installDir(m_installFolder);
    QString installedPath = installDir.filePath("installed");

    // Ensure installed folder exists
    if (!QDir(installedPath).exists()) {
        if (!installDir.mkpath("installed")) {
            return false;
        }
    }

    QFileInfo info(filePath);
    QString destPath = QDir(installedPath).filePath(info.fileName());

    // If destination already exists, remove it first
    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }

    return QFile::rename(filePath, destPath);
}

void InstallConduit::ensureFoldersExist()
{
    if (m_installFolder.isEmpty()) {
        return;
    }

    QDir dir(m_installFolder);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // Also create the "installed" subfolder
    QString installedPath = dir.filePath("installed");
    if (!QDir(installedPath).exists()) {
        dir.mkpath("installed");
    }
}

} // namespace Sync
