#ifndef WILDPALMS_SYNC_PALMRECORD_H
#define WILDPALMS_SYNC_PALMRECORD_H

#include <cstdint>

#include <QByteArray>
#include <QDateTime>

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
};

} // namespace WildPalms::PalmSync

#endif // WILDPALMS_SYNC_PALMRECORD_H
