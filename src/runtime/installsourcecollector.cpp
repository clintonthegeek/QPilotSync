#include "installsourcecollector.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace WildPalms {

InstallSourceCollector::Result
InstallSourceCollector::collect(const QString          &folderPath,
                                  BackendPluginManager   *manager)
{
    Result r;
    QStringList folderSourcedPaths;
    auto folderEntries = scanFolder(folderPath, &folderSourcedPaths);
    r.files               = folderEntries;
    r.folderSourcedPaths  = folderSourcedPaths;

    if (manager) {
        r.tempDir = QSharedPointer<QTemporaryDir>::create();
        if (r.tempDir->isValid()) {
            r.files += drainPluginBlobs(manager, r.tempDir.data());
        }
    }
    return r;
}

QList<InstallSourceCollector::FileEntry>
InstallSourceCollector::scanFolder(const QString &folderPath,
                                      QStringList    *outFolderPaths)
{
    QList<FileEntry> entries;
    if (folderPath.isEmpty()) return entries;

    QDir dir(folderPath);
    if (!dir.exists()) return entries;

    const QStringList filters{
        QStringLiteral("*.prc"), QStringLiteral("*.pdb"),
        QStringLiteral("*.PRC"), QStringLiteral("*.PDB")
    };
    const auto files = dir.entryInfoList(filters,
                                           QDir::Files | QDir::Readable);
    for (const auto &fi : files) {
        FileEntry e;
        e.path        = fi.absoluteFilePath();
        e.displayName = fi.fileName();
        entries.append(e);
        if (outFolderPaths) outFolderPaths->append(e.path);
    }
    return entries;
}

QList<InstallSourceCollector::FileEntry>
InstallSourceCollector::drainPluginBlobs(BackendPluginManager * /*manager*/,
                                            QTemporaryDir       * /*dir*/)
{
    // Implemented in Task 5.
    return {};
}

bool InstallSourceCollector::isInstallableType(const QString &type)
{
    return type.endsWith(QStringLiteral("-prc"))
        || type.endsWith(QStringLiteral("-pdb"))
        || type.contains(QStringLiteral("-bootstrap"));
}

QString InstallSourceCollector::inferExtension(const QString &type,
                                                  const QString &displayName)
{
    if (type.endsWith(QStringLiteral("-prc"))) return QStringLiteral(".prc");
    if (type.endsWith(QStringLiteral("-pdb"))) return QStringLiteral(".pdb");
    if (type.contains(QStringLiteral("-bootstrap"))) {
        const QFileInfo fi(displayName);
        const QString ext = fi.suffix();
        if (!ext.isEmpty()) return QStringLiteral(".") + ext;
        return QStringLiteral(".prc");
    }
    return QStringLiteral(".pdb");
}

void InstallSourceCollector::moveSucceededToInstalled(
    const Result      &result,
    const QStringList &succeededPaths)
{
    const QSet<QString> succeeded(succeededPaths.begin(), succeededPaths.end());
    for (const QString &path : result.folderSourcedPaths) {
        if (!succeeded.contains(path)) continue;

        const QFileInfo fi(path);
        const QString folder = fi.absolutePath();
        const QString installedDir = QDir(folder).filePath(QStringLiteral("installed"));
        QDir().mkpath(installedDir);
        const QString dest = QDir(installedDir).filePath(fi.fileName());
        if (QFile::exists(dest)) QFile::remove(dest);
        QFile::rename(path, dest);
    }
}

} // namespace WildPalms
