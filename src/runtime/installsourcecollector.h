#ifndef WILDPALMS_RUNTIME_INSTALLSOURCECOLLECTOR_H
#define WILDPALMS_RUNTIME_INSTALLSOURCECOLLECTOR_H

#include <QList>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

namespace WildPalms {

class BackendPluginManager;

/**
 * @brief Aggregates installable files from a folder + cross-plugin
 *        blob backends into a flat list ready for InstallActionPlugin.
 *
 * Folder-sourced files are tracked separately so successful installs
 * can be moved into the legacy `installed/` subfolder. Plugin-blob
 * records are written to a fresh QTemporaryDir owned by the Result;
 * lifetime ends with the Result.
 */
class InstallSourceCollector
{
public:
    struct FileEntry {
        QString path;
        QString displayName;
    };

    struct Result {
        QList<FileEntry>                files;
        QSharedPointer<QTemporaryDir>   tempDir;
        QStringList                     folderSourcedPaths;
    };

    InstallSourceCollector() = default;

    /// Aggregate sources. `folderPath` may be empty (skip folder
    /// scan). `manager` may be null (skip plugin scan).
    Result collect(const QString          &folderPath,
                   BackendPluginManager   *manager);

    /// Move folder-sourced files whose paths are in `succeededPaths`
    /// from `<folder>/X` to `<folder>/installed/X`. Creates the
    /// `installed/` subdir if absent.
    void moveSucceededToInstalled(const Result      &result,
                                   const QStringList &succeededPaths);

private:
    QList<FileEntry> scanFolder(const QString &folderPath,
                                  QStringList    *outFolderPaths);
    QList<FileEntry> drainPluginBlobs(BackendPluginManager *manager,
                                        QTemporaryDir       *dir);
    static bool      isInstallableType(const QString &type);
    static QString   inferExtension(const QString &type,
                                      const QString &displayName);
};

} // namespace WildPalms

#endif // WILDPALMS_RUNTIME_INSTALLSOURCECOLLECTOR_H
