#ifndef WILDPALMS_SYNC_PALMRECORD_H
#define WILDPALMS_SYNC_PALMRECORD_H

#include <cstdint>

#include <QByteArray>
#include <QDataStream>
#include <QDateTime>
#include <QIODevice>

namespace WildPalms::PalmSync {

/**
 * @brief Device-side representation of a Palm database record.
 *
 * Carries the fields that survive a Palm DLP round-trip. Used across
 * the IPalmDatabaseAccess interface; converted to/from
 * Kalburator::Sync::BackendRecord inside PalmBackend.
 *
 * This is a scaffold-phase type. It is intentionally distinct from
 * WP's existing ::PilotRecord class (which wraps pilot-link and lives
 * in WildPalmsCore); the two are bridged in Phase E.4 when the real
 * DLP adapter lands.
 */
struct PalmRecord {
    std::uint32_t recordId = 0;   ///< 32-bit Palm unique ID.
    std::uint8_t  category  = 0;  ///< 4-bit category slot (0..15).
    std::uint8_t  attributes = 0; ///< Flag byte: Deleted / Dirty / Secret / Archived.
    QByteArray    data;           ///< Record payload.
    QDateTime     lastModified;   ///< Mock-tracked; real DLP fills from
                                  ///  database modification time.

    // Attribute-flag constants mirror ::PilotRecord::Attribute so
    // callers bridging to pilot-link see the same bit layout.
    static constexpr std::uint8_t AttrDeleted  = 0x80;
    static constexpr std::uint8_t AttrDirty    = 0x40;
    static constexpr std::uint8_t AttrBusy     = 0x20;
    static constexpr std::uint8_t AttrSecret   = 0x10;
    static constexpr std::uint8_t AttrArchived = 0x08;

    bool isDeleted()  const { return attributes & AttrDeleted;  }
    bool isDirty()    const { return attributes & AttrDirty;    }
    bool isSecret()   const { return attributes & AttrSecret;   }
    bool isArchived() const { return attributes & AttrArchived; }

    bool operator==(const PalmRecord &other) const = default;

    /**
     * @brief Serialize this record into a self-describing byte string
     *        for in-process exchange across libkalburator's
     *        TransformationStage boundary.
     *
     * Format is QDataStream-based (Qt_6_0 version), carrying
     * recordId / category / attributes / data / lastModified in that
     * order. The exact bytes are an internal contract between WP's
     * ContactsBlobBackend and the palm↔vcard4 transformation stages
     * (Phase Ia, Tasks 11/14/16/17). Not stable on-disk; do not
     * persist these bytes.
     *
     * Round-trip: fromWireBytes(toWireBytes(r)) == r.
     */
    QByteArray toWireBytes() const
    {
        QByteArray out;
        QDataStream ds(&out, QIODevice::WriteOnly);
        ds.setVersion(QDataStream::Qt_6_0);
        ds << static_cast<quint32>(recordId)
           << static_cast<quint8>(category)
           << static_cast<quint8>(attributes)
           << data
           << lastModified;
        return out;
    }

    /// Inverse of toWireBytes(). Returns a default-constructed
    /// PalmRecord if the stream is malformed (best-effort: callers
    /// should validate by checking that the result round-trips).
    static PalmRecord fromWireBytes(const QByteArray &bytes)
    {
        PalmRecord r;
        if (bytes.isEmpty())
            return r;
        QDataStream ds(bytes);
        ds.setVersion(QDataStream::Qt_6_0);
        quint32 rid = 0;
        quint8 cat = 0;
        quint8 attrs = 0;
        ds >> rid >> cat >> attrs >> r.data >> r.lastModified;
        if (ds.status() != QDataStream::Ok)
            return PalmRecord{};
        r.recordId = rid;
        r.category = cat;
        r.attributes = attrs;
        return r;
    }
};

} // namespace WildPalms::PalmSync

#endif // WILDPALMS_SYNC_PALMRECORD_H
