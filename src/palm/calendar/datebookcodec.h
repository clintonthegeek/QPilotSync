#ifndef WILDPALMS_CALENDAR_DATEBOOKCODEC_H
#define WILDPALMS_CALENDAR_DATEBOOKCODEC_H

#include <KCalendarCore/Event>

#include "palmrecord.h"

namespace WildPalms::PalmCalendar {

/**
 * @brief Pure byte-level codec: Palm Datebook record <-> KCalendarCore::Event.
 *
 * Uses pisock's `pi-datebook.h` (pack_Appointment / unpack_Appointment)
 * for the bit-level Datebook record layout. Stateless; caller owns
 * the `PalmRecord` and the resulting `Event::Ptr`.
 *
 * Palm record ID round-trip: stashed as
 * X-WP-PALM-RECORDID on the Event; if present on encode, preserved;
 * if absent on encode, the PalmRecord's recordId is zero and the
 * device assigns on write.
 *
 * Category slot round-trip: decoded from `PalmRecord::category` and
 * stashed as X-WP-PALM-CATEGORY-SLOT (so downstream callers that
 * lose the calendar-ID context can still recover it). On encode, the
 * `slot` parameter wins over any property.
 */
class DatebookCodec {
public:
    static constexpr const char *RecordIdProperty    = "X-WP-PALM-RECORDID";
    static constexpr const char *CategorySlotProperty = "X-WP-PALM-CATEGORY-SLOT";

    struct DecodeResult {
        KCalendarCore::Event::Ptr event;  ///< null on failure
        int  slot = 0;                    ///< PalmRecord::category
        QString failureReason;            ///< empty on success
        bool isValid() const { return !event.isNull(); }
    };

    /// Decode Palm Datebook record bytes into an Event. Records with
    /// `AttrDeleted` set return a null-event DecodeResult with
    /// failureReason="deleted" — callers typically skip these rather
    /// than surface as tombstones.
    static DecodeResult decode(const WildPalms::PalmSync::PalmRecord &record);

    /// Encode an Event into a PalmRecord with the given category slot.
    /// `slot` is clamped to [0..15]. `recordId` is copied from the
    /// Event's X-WP-PALM-RECORDID property if present (and parseable),
    /// else 0. Other attributes (Deleted/Dirty/Secret/Archived) are
    /// left as 0 — preserving on write-back is an E.7 concern.
    static WildPalms::PalmSync::PalmRecord encode(
        const KCalendarCore::Event::Ptr &event, int slot);
};

} // namespace WildPalms::PalmCalendar

#endif // WILDPALMS_CALENDAR_DATEBOOKCODEC_H
