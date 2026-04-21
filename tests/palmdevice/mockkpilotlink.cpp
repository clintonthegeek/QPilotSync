#include "mockkpilotlink.h"

#include <cstring>

MockKPilotLink::MockKPilotLink(QObject *parent)
    : KPilotLink(parent)
{
}

MockKPilotLink::~MockKPilotLink() = default;

bool MockKPilotLink::openConnection()
{
    m_connected = true;
    setStatus(DeviceOpen);
    return true;
}

void MockKPilotLink::closeConnection()
{
    m_connected = false;
    setStatus(Init);
}

bool MockKPilotLink::readUserInfo(struct PilotUser &)       { return false; }
bool MockKPilotLink::writeUserInfo(const struct PilotUser &) { return false; }
bool MockKPilotLink::readSysInfo(struct SysInfo &)          { return false; }
bool MockKPilotLink::readStorageInfo(int, struct CardInfo &) { return false; }

int MockKPilotLink::openDatabase(const QString &dbName, bool /*readWrite*/)
{
    if (!m_dbs.contains(dbName)) return -1;
    const int h = m_nextHandle++;
    m_handles.insert(h, dbName);
    return h;
}

bool MockKPilotLink::closeDatabase(int handle)
{
    return m_handles.remove(handle) > 0;
}

QStringList MockKPilotLink::listDatabases()
{
    return QStringList(m_dbs.keyBegin(), m_dbs.keyEnd());
}

MockKPilotLink::Database *MockKPilotLink::dbForHandle(int handle)
{
    const auto it = m_handles.constFind(handle);
    if (it == m_handles.constEnd()) return nullptr;
    auto dbIt = m_dbs.find(*it);
    return dbIt == m_dbs.end() ? nullptr : &*dbIt;
}

const MockKPilotLink::Database *MockKPilotLink::dbForHandle(int handle) const
{
    const auto it = m_handles.constFind(handle);
    if (it == m_handles.constEnd()) return nullptr;
    const auto dbIt = m_dbs.constFind(*it);
    return dbIt == m_dbs.constEnd() ? nullptr : &*dbIt;
}

QList<PilotRecord*> MockKPilotLink::readAllRecords(int dbHandle)
{
    const Database *db = dbForHandle(dbHandle);
    if (!db) return {};
    QList<PilotRecord*> out;
    out.reserve(db->rows.size());
    for (auto it = db->rows.constBegin(); it != db->rows.constEnd(); ++it) {
        out.append(new PilotRecord(it->recordId, it->category,
                                   it->attributes, it->data));
    }
    return out;
}

PilotRecord *MockKPilotLink::readRecordByIndex(int dbHandle, int index)
{
    const Database *db = dbForHandle(dbHandle);
    if (!db) return nullptr;
    if (index < 0 || index >= db->rows.size()) return nullptr;
    auto it = db->rows.constBegin();
    std::advance(it, index);
    return new PilotRecord(it->recordId, it->category,
                           it->attributes, it->data);
}

PilotRecord *MockKPilotLink::readRecordById(int dbHandle, int recordId)
{
    const Database *db = dbForHandle(dbHandle);
    if (!db) return nullptr;
    const auto it = db->rows.constFind(recordId);
    if (it == db->rows.constEnd()) return nullptr;
    return new PilotRecord(it->recordId, it->category,
                           it->attributes, it->data);
}

bool MockKPilotLink::writeRecord(int dbHandle, PilotRecord *record)
{
    Database *db = dbForHandle(dbHandle);
    if (!db || !record) return false;

    int id = record->recordId();
    if (id == 0) {
        id = db->nextId++;
        record->setRecordId(id);
    } else {
        db->nextId = std::max(db->nextId, id + 1);
    }
    Row row{id, record->category(), record->attributes(), record->data()};
    db->rows.insert(id, row);
    return true;
}

bool MockKPilotLink::deleteRecord(int dbHandle, int recordId)
{
    Database *db = dbForHandle(dbHandle);
    if (!db) return false;
    return db->rows.remove(recordId) > 0;
}

QList<PilotRecord*> MockKPilotLink::readModifiedRecords(int dbHandle)
{
    // No modified-flag bookkeeping in the scaffold mock; return all.
    return readAllRecords(dbHandle);
}

bool MockKPilotLink::resetDBIndex(int) { return true; }

bool MockKPilotLink::readAppBlock(int dbHandle, unsigned char *buffer,
                                  size_t *size)
{
    const Database *db = dbForHandle(dbHandle);
    if (!db || !size) return false;
    const auto len = static_cast<size_t>(db->appBlock.size());
    if (*size < len) {
        *size = len;
        return false;
    }
    std::memcpy(buffer, db->appBlock.constData(), len);
    *size = len;
    return true;
}

bool MockKPilotLink::writeAppBlock(int dbHandle, const unsigned char *buffer,
                                   size_t size)
{
    Database *db = dbForHandle(dbHandle);
    if (!db) return false;
    db->appBlock = QByteArray(reinterpret_cast<const char *>(buffer),
                              static_cast<int>(size));
    return true;
}

bool MockKPilotLink::cleanUpDatabase(int) { return true; }
bool MockKPilotLink::resetSyncFlags(int)  { return true; }

bool MockKPilotLink::seedDatabase(const QString &dbName)
{
    if (m_dbs.contains(dbName)) return false;
    Database db;
    db.name = dbName;
    m_dbs.insert(dbName, db);
    return true;
}

bool MockKPilotLink::seedRecord(const QString &dbName, int recordId,
                                int category, int attributes,
                                const QByteArray &data)
{
    if (!m_dbs.contains(dbName)) return false;
    Database &db = m_dbs[dbName];
    if (db.rows.contains(recordId)) return false;
    Row row{recordId, category, attributes, data};
    db.rows.insert(recordId, row);
    db.nextId = std::max(db.nextId, recordId + 1);
    return true;
}

bool MockKPilotLink::hasRecord(const QString &dbName, int recordId) const
{
    const auto it = m_dbs.constFind(dbName);
    if (it == m_dbs.constEnd()) return false;
    return it->rows.contains(recordId);
}

QByteArray MockKPilotLink::recordData(const QString &dbName,
                                      int recordId) const
{
    const auto it = m_dbs.constFind(dbName);
    if (it == m_dbs.constEnd()) return {};
    const auto rIt = it->rows.constFind(recordId);
    if (rIt == it->rows.constEnd()) return {};
    return rIt->data;
}
