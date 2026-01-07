#ifndef KPILOTLINK_H
#define KPILOTLINK_H

#include <QObject>
#include <QString>
#include <QList>

// Forward declarations
struct PilotUser;
struct SysInfo;
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

    // Database operations
    virtual int openDatabase(const QString &dbName) = 0;
    virtual bool closeDatabase(int handle) = 0;
    virtual QStringList listDatabases() = 0;

    // Record operations
    virtual QList<PilotRecord*> readAllRecords(int dbHandle) = 0;
    virtual PilotRecord* readRecordByIndex(int dbHandle, int index) = 0;
    virtual PilotRecord* readRecordById(int dbHandle, int recordId) = 0;
    virtual bool writeRecord(int dbHandle, PilotRecord *record) = 0;
    virtual bool deleteRecord(int dbHandle, int recordId) = 0;

    // AppInfo block (categories, etc.)
    virtual bool readAppBlock(int dbHandle, unsigned char *buffer, size_t *size) = 0;
    virtual bool writeAppBlock(int dbHandle, const unsigned char *buffer, size_t size) = 0;

    // Sync operations
    virtual bool beginSync() = 0;
    virtual bool endSync() = 0;

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
