#include "palmbackend.h"

#include <QCryptographicHash>

#include "backendrecord.h"
#include "collectioninfo.h"
#include "ipalmdatabaseaccess.h"
#include "palmrecord.h"

namespace WildPalms::PalmSync {

namespace {
constexpr const char kCollectionPrefix[] = "palm:";
} // namespace

PalmBackend::PalmBackend(IPalmDatabaseAccess *device, QObject *parent)
    : Kalburator::Sync::IBlobBackend(parent)
    , m_device(device)
{
}

PalmBackend::~PalmBackend() = default;

QString PalmBackend::backendId() const
{
    return QStringLiteral("palm");
}

QString PalmBackend::displayName() const
{
    return QStringLiteral("Palm OS Device");
}

bool PalmBackend::isAvailable() const
{
    return m_device != nullptr;
}

// --- ID encoding ---

QString PalmBackend::encodeCollectionId(const QString &dbName)
{
    // "DatebookDB" -> "palm:datebook", "MemoDB" -> "palm:memo" etc.
    QString bare = dbName;
    if (bare.endsWith(QStringLiteral("DB"))) {
        bare.chop(2);
    }
    return QStringLiteral("%1%2").arg(QString::fromLatin1(kCollectionPrefix),
                                      bare.toLower());
}

bool PalmBackend::decodeCollectionId(const QString &collectionId,
                                     QString *dbNameOut)
{
    const auto prefix = QString::fromLatin1(kCollectionPrefix);
    if (!collectionId.startsWith(prefix)) return false;
    const auto bare = collectionId.mid(prefix.size());
    if (bare.isEmpty()) return false;
    if (bare.contains(QLatin1Char(':'))) return false; // that's a record id
    if (dbNameOut) {
        // Round-trip mapping: "memo" -> "MemoDB". Capitalise the first
        // letter and append "DB".
        QString titled = bare;
        titled[0] = titled[0].toUpper();
        *dbNameOut = titled + QStringLiteral("DB");
    }
    return true;
}

QString PalmBackend::encodeRecordId(const QString &dbName,
                                    std::uint32_t recordId)
{
    return QStringLiteral("%1:%2").arg(encodeCollectionId(dbName))
                                  .arg(recordId);
}

bool PalmBackend::decodeRecordId(const QString &encoded,
                                 QString *dbNameOut,
                                 std::uint32_t *recordIdOut)
{
    // Format: "palm:<bare>:<numeric>"
    const auto parts = encoded.split(QLatin1Char(':'));
    if (parts.size() != 3) return false;
    if (parts[0] != QStringLiteral("palm")) return false;
    bool ok = false;
    const auto numeric = parts[2].toUInt(&ok);
    if (!ok) return false;
    if (dbNameOut) {
        QString titled = parts[1];
        titled[0] = titled[0].toUpper();
        *dbNameOut = titled + QStringLiteral("DB");
    }
    if (recordIdOut) *recordIdOut = numeric;
    return true;
}

// --- Empty stubs; filled in by Tasks 5-7 ---

QList<Kalburator::Sync::CollectionInfo> PalmBackend::availableCollections()
{
    return {};
}

Kalburator::Sync::CollectionInfo PalmBackend::collectionInfo(const QString &)
{
    return {};
}

QString PalmBackend::createCollection(const Kalburator::Sync::CollectionInfo &)
{
    return {};
}

QList<Kalburator::Sync::BackendRecord> PalmBackend::loadRecords(const QString &)
{
    return {};
}

std::optional<Kalburator::Sync::BackendRecord> PalmBackend::loadRecord(
    const QString &)
{
    return std::nullopt;
}

QString PalmBackend::createRecord(const QString &,
                                  const Kalburator::Sync::BackendRecord &)
{
    return {};
}

bool PalmBackend::updateRecord(const Kalburator::Sync::BackendRecord &)
{
    return false;
}

bool PalmBackend::deleteRecord(const QString &)
{
    return false;
}

QList<Kalburator::Sync::BackendRecord> PalmBackend::modifiedSince(
    const QString &, const QDateTime &)
{
    return {};
}

QStringList PalmBackend::deletedSince(const QString &, const QDateTime &)
{
    return {};
}

bool PalmBackend::supportsDeleteTracking() const
{
    return m_device && m_device->supportsDeleteTracking();
}

} // namespace WildPalms::PalmSync
