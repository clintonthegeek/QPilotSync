#ifndef INSTALLCONDUIT_H
#define INSTALLCONDUIT_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDir>

class KPilotDeviceLink;

namespace Sync {

/**
 * @brief Result of a file installation attempt
 */
struct InstallResult
{
    QString fileName;
    bool success = false;
    QString errorMessage;
};

/**
 * @brief Conduit for installing .prc/.pdb files to Palm devices
 *
 * This conduit watches an "install" folder in the sync directory.
 * During sync, any .prc or .pdb files found there are installed
 * to the Palm device. Successfully installed files are moved to
 * an "installed" subfolder to prevent re-installation.
 *
 * Unlike regular conduits, this doesn't sync records - it just
 * transfers complete database files to the Palm.
 *
 * Usage:
 * 1. Drop .prc/.pdb files into <sync_folder>/install/
 * 2. Run HotSync
 * 3. Files are installed and moved to <sync_folder>/install/installed/
 */
class InstallConduit : public QObject
{
    Q_OBJECT

public:
    explicit InstallConduit(QObject *parent = nullptr);
    ~InstallConduit() override = default;

    // ========== Conduit Identity ==========

    QString conduitId() const { return "install"; }
    QString displayName() const { return "Install Files"; }

    // ========== Configuration ==========

    /**
     * @brief Set the install folder path
     * @param path Path to folder containing .prc/.pdb files to install
     */
    void setInstallFolder(const QString &path);

    /**
     * @brief Get the install folder path
     */
    QString installFolder() const { return m_installFolder; }

    /**
     * @brief Set whether to move files after successful install
     *
     * If true (default), files are moved to "installed" subfolder.
     * If false, files are deleted after successful install.
     */
    void setKeepInstalledFiles(bool keep) { m_keepInstalledFiles = keep; }
    bool keepInstalledFiles() const { return m_keepInstalledFiles; }

    // ========== Operations ==========

    /**
     * @brief Get list of files pending installation
     *
     * Scans the install folder for .prc and .pdb files.
     */
    QStringList pendingFiles() const;

    /**
     * @brief Check if there are files to install
     */
    bool hasPendingFiles() const { return !pendingFiles().isEmpty(); }

    /**
     * @brief Install all pending files to the Palm device
     *
     * @param socket The pilot-link socket descriptor (from KPilotDeviceLink)
     * @return List of results for each file
     */
    QList<InstallResult> installAll(int socket);

    /**
     * @brief Install a single file to the Palm device
     *
     * @param filePath Path to the .prc/.pdb file
     * @param socket The pilot-link socket descriptor
     * @return Result of the installation
     */
    InstallResult installFile(const QString &filePath, int socket);

signals:
    void logMessage(const QString &message);
    void errorOccurred(const QString &error);
    void progressUpdated(int current, int total, const QString &fileName);
    void fileInstalled(const QString &fileName, bool success);

private:
    /**
     * @brief Move a successfully installed file to the "installed" folder
     */
    bool moveToInstalled(const QString &filePath);

    /**
     * @brief Ensure the install and installed folders exist
     */
    void ensureFoldersExist();

    QString m_installFolder;
    bool m_keepInstalledFiles = true;
};

} // namespace Sync

#endif // INSTALLCONDUIT_H
