#include "installsourcecollector.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

// K.8b T13: BackendPluginManager + IBackendPluginV2 deleted along with the
// V2 plugin ABI. The plugin-blob drain path (drainPluginBlobs) was the only
// consumer of those types here; T14 wires up the replacement (Kalburator::
// Plugin's blob aggregation) once the new install action plugin lands.
// Until then, collect() degrades gracefully when manager == nullptr (which
// is now always the case at every call site).

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

    // K.8b T13: plugin-blob drain path disabled — BackendPluginManager gone.
    // T14 reintroduces blob aggregation via Kalburator::Plugin.
    Q_UNUSED(manager);
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

// K.8b T13: drainPluginBlobs removed — V2 plugin ABI deleted. Replacement
// lands in T14 via Kalburator::Plugin's blob aggregation API.

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
