#ifndef WILDPALMS_DEVICE_PALMRECORD_BRIDGE_H
#define WILDPALMS_DEVICE_PALMRECORD_BRIDGE_H

#include "pilotrecord.h"
#include "palmrecord.h"

namespace WildPalms::PalmDevice {

/// Convert pilot-link ::PilotRecord (WildPalmsCore) into
/// WildPalms::PalmSync::PalmRecord (WildPalmsPalmSync). lastModified
/// is left invalid because the Palm record layer does not carry a
/// per-record modification time; callers may stamp it from context
/// (e.g. the database header's modification time, or "now" for
/// freshly-read records).
WildPalms::PalmSync::PalmRecord fromPilotRecord(const PilotRecord &src);

/// Inverse: build a ::PilotRecord (ownership-free, caller-constructed)
/// from a PalmRecord. The returned PilotRecord is NOT heap-allocated;
/// callers that need the pilot-link DLP API's `PilotRecord *` shape
/// should wrap with `new PilotRecord(toPilotRecord(pr))`.
///
/// If src.recordId is 0, the returned PilotRecord's id is 0 — DLP
/// interprets this as "assign a new record ID on write."
PilotRecord toPilotRecord(const WildPalms::PalmSync::PalmRecord &src);

} // namespace WildPalms::PalmDevice

#endif // WILDPALMS_DEVICE_PALMRECORD_BRIDGE_H
