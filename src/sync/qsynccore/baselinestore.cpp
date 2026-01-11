#include "baselinestore.h"

namespace QSyncCore {

BaselineStore::BaselineStore(QObject *parent)
    : QObject(parent)
{
}

void BaselineStore::saveBaseline(const QMap<RecordId, QString> &hashes)
{
    m_hashes = hashes;
    emit baselineChanged();
}

void BaselineStore::setHash(const RecordId &recordId, const QString &hash)
{
    m_hashes[recordId] = hash;
    emit baselineChanged();
}

void BaselineStore::removeHash(const RecordId &recordId)
{
    if (m_hashes.remove(recordId) > 0) {
        emit baselineChanged();
    }
}

QString BaselineStore::hash(const RecordId &recordId) const
{
    return m_hashes.value(recordId);
}

bool BaselineStore::hasChanged(const RecordId &recordId, const QString &currentHash) const
{
    if (!m_hashes.contains(recordId)) {
        return true;  // New record (not in baseline)
    }
    return m_hashes[recordId] != currentHash;
}

bool BaselineStore::hasRecord(const RecordId &recordId) const
{
    return m_hashes.contains(recordId);
}

QStringList BaselineStore::allRecordIds() const
{
    return m_hashes.keys();
}

QJsonObject BaselineStore::toJson() const
{
    QJsonObject obj;
    for (auto it = m_hashes.constBegin(); it != m_hashes.constEnd(); ++it) {
        obj[it.key()] = it.value();
    }
    return obj;
}

int BaselineStore::fromJson(const QJsonObject &json)
{
    clear();

    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        m_hashes[it.key()] = it.value().toString();
    }

    return m_hashes.size();
}

void BaselineStore::clear()
{
    m_hashes.clear();
    emit baselineChanged();
}

} // namespace QSyncCore
