#ifndef WILDPALMS_TESTS_MOCKKPILOTLINK_H
#define WILDPALMS_TESTS_MOCKKPILOTLINK_H

#include <QHash>

#include "kpilotlink.h"
#include "pilotrecord.h"

/**
 * @brief In-memory KPilotLink implementation for Phase E.4 tests.
 *
 * Tracks databases as per-DB record tables. Each database yields a
 * non-zero handle assigned at open time. Read methods return
 * heap-allocated PilotRecord*, matching KPilotLink's contract.
 *
 * Not thread-safe. Tests drive it from a single thread.
 */
class MockKPilotLink : public KPilotLink
{
    Q_OBJECT
public:
    explicit MockKPilotLink(QObject *parent = nullptr);
    ~MockKPilotLink() override;

    // Connection management
    bool openConnection() override;
    void closeConnection() override;
    LinkStatus status() const override { return m_status; }

    // User / sys info — stubbed for tests that don't need them.
    bool readUserInfo(struct PilotUser &user) override;
    bool writeUserInfo(const struct PilotUser &user) override;
    bool readSysInfo(struct SysInfo &sysInfo) override;
    bool readStorageInfo(int cardNo, struct CardInfo &cardInfo) override;

    // Database operations
    int openDatabase(const QString &dbName, bool readWrite = false) override;
    bool closeDatabase(int handle) override;
    QStringList listDatabases() override;

    // Record operations
    QList<PilotRecord*> readAllRecords(int dbHandle) override;
    PilotRecord* readRecordByIndex(int dbHandle, int index) override;
    PilotRecord* readRecordById(int dbHandle, int recordId) override;
    bool writeRecord(int dbHandle, PilotRecord *record) override;
    bool deleteRecord(int dbHandle, int recordId) override;
    QList<PilotRecord*> readModifiedRecords(int dbHandle) override;
    bool resetDBIndex(int dbHandle) override;

    // AppInfo
    bool readAppBlock(int dbHandle, unsigned char *buffer,
                      size_t *size) override;
    bool writeAppBlock(int dbHandle, const unsigned char *buffer,
                       size_t size) override;

    // Sync lifecycle
    bool beginSync() override { return true; }
    bool endSync() override { return true; }

    // State
    bool isConnected() const override { return m_connected; }
    bool cleanUpDatabase(int dbHandle) override;
    bool resetSyncFlags(int dbHandle) override;

    // --- Test helpers ---

    /// Create an empty database (fails if name already taken).
    bool seedDatabase(const QString &dbName);

    /// Seed a record directly (bypasses openDatabase). recordId
    /// must be non-zero and unique within the database.
    bool seedRecord(const QString &dbName, int recordId, int category,
                    int attributes, const QByteArray &data);

    /// Raw access for test assertions.
    bool hasRecord(const QString &dbName, int recordId) const;
    QByteArray recordData(const QString &dbName, int recordId) const;

private:
    struct Row {
        int         recordId;
        int         category;
        int         attributes;
        QByteArray  data;
    };

    struct Database {
        QString           name;
        QHash<int, Row>   rows;     // recordId -> Row
        QByteArray        appBlock;
        int               nextId = 1;
    };

    bool    m_connected = false;
    QHash<QString, Database> m_dbs;
    QHash<int, QString>      m_handles; // handle -> dbName
    int     m_nextHandle = 1;

    Database *dbForHandle(int handle);
    const Database *dbForHandle(int handle) const;
};

#endif // WILDPALMS_TESTS_MOCKKPILOTLINK_H
