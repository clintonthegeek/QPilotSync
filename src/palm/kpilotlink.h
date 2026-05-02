#ifndef KPILOTLINK_H
#define KPILOTLINK_H

#include <QObject>
#include <QString>
#include <QList>

// Forward declarations
struct PilotUser;
struct SysInfo;
struct CardInfo;
class PilotRecord;

/**
 * @brief Abstract interface for Palm device communication
 *
 * This class provides a device-independent interface for communicating
 * with Palm devices. Implementations can use real hardware (KPilotDeviceLink)
 * or filesystem-based testing (KPilotLocalLink).
 */
class KPilotLink : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Connection state machine states
     */
    enum LinkStatus {
        Init,                  // Newly created, not connected
        WaitingForDevice,      // Listening for device
        FoundDevice,           // Device detected
        CreatedSocket,         // Socket created
        DeviceOpen,            // Device file opened
        AcceptedDevice,        // Connection accepted
        SyncDone,              // Sync completed
        PilotLinkError         // Error occurred
    };
    Q_ENUM(LinkStatus)

    explicit KPilotLink(QObject *parent = nullptr);
    virtual ~KPilotLink();

    // Connection management
    virtual bool openConnection() = 0;
    virtual void closeConnection() = 0;
    virtual LinkStatus status() const = 0;

    // User information
    virtual bool readUserInfo(struct PilotUser &user) = 0;
    virtual bool writeUserInfo(const struct PilotUser &user) = 0;
    virtual bool readSysInfo(struct SysInfo &sysInfo) = 0;
    virtual bool readStorageInfo(int cardNo, struct CardInfo &cardInfo) = 0;

    // Database operations
    virtual int openDatabase(const QString &dbName, bool readWrite = false) = 0;
    virtual bool closeDatabase(int handle) = 0;
    virtual QStringList listDatabases() = 0;

    // Record operations
    virtual QList<PilotRecord*> readAllRecords(int dbHandle) = 0;
    virtual PilotRecord* readRecordByIndex(int dbHandle, int index) = 0;
    virtual PilotRecord* readRecordById(int dbHandle, int recordId) = 0;
    virtual bool writeRecord(int dbHandle, PilotRecord *record) = 0;
    virtual bool deleteRecord(int dbHandle, int recordId) = 0;
    virtual QList<PilotRecord*> readModifiedRecords(int dbHandle) = 0;
    virtual bool resetDBIndex(int dbHandle) = 0;

    // AppInfo block (categories, etc.)
    virtual bool readAppBlock(int dbHandle, unsigned char *buffer, size_t *size) = 0;
    virtual bool writeAppBlock(int dbHandle, const unsigned char *buffer, size_t size) = 0;

    // Sync operations
    virtual bool beginSync() = 0;
    virtual bool endSync() = 0;

    // Post-sync operations
    virtual bool isConnected() const = 0;
    virtual bool cleanUpDatabase(int dbHandle) = 0;
    virtual bool resetSyncFlags(int dbHandle) = 0;

    // Raw database file transfer (backup / restore)
    // retrieveDatabase: download the named database from the device and write
    // it as a .pdb/.prc file to destPath.  Returns false on any error;
    // returns true (and writes nothing) if the database has the copy-prevention
    // flag set and should be silently skipped.
    virtual bool retrieveDatabase(const QString &dbName, const QString &destPath) = 0;
    // installFile: upload a .pdb/.prc file from filePath onto the device.
    virtual bool installFile(const QString &filePath) = 0;

    // Tickle coordination: pause/resume the keep-alive tickle around bulk
    // socket operations (backup/restore) that need exclusive socket access.
    // Default no-ops — real devices override via KPilotDeviceLink.
    virtual void pauseTickle() {}
    virtual void resumeTickle() {}

signals:
    void statusChanged(LinkStatus status);
    void deviceReady(const QString &userName, const QString &deviceName);
    void logMessage(const QString &message);
    void errorOccurred(const QString &errorMsg);

protected:
    LinkStatus m_status;
    QString m_lastError;

    void setStatus(LinkStatus newStatus);
    void setError(const QString &error);
};

#endif // KPILOTLINK_H
