#ifndef WILDPALMS_CODECS_KDE_PIM_CONVERT_H
#define WILDPALMS_CODECS_KDE_PIM_CONVERT_H

#include <KContacts/Addressee>
#include <KCalendarCore/Todo>

#include "contactcodec.h"
#include "todocodec.h"

namespace WildPalms::PalmCodecs {

/// Contact -> KContacts::Addressee. Best-effort mapping. Lossy fields
/// (`showPhone`, `custom1..4`, non-standard phone labels) are stashed
/// in `X-PALM-*` custom fields so the reverse conversion is
/// information-preserving.
KContacts::Addressee toAddressee(const Contact &c);

/// KContacts::Addressee -> Contact. Reads `X-PALM-*` custom fields to
/// recover the Palm-specific data when present; defaults otherwise.
Contact fromAddressee(const KContacts::Addressee &a);

/// Todo -> KCalendarCore::Todo. Priority maps 1:1 for 1..5. Indefinite
/// due maps to "no due date". `isComplete` maps to
/// `KCalendarCore::Todo::Completed`.
KCalendarCore::Todo::Ptr toKCalTodo(const Todo &t);

/// KCalendarCore::Todo -> Todo. Priority > 5 clamps to 5 on reverse
/// (iCal's 6..9 have no Palm equivalent).
Todo fromKCalTodo(const KCalendarCore::Todo::Ptr &kcal);

} // namespace WildPalms::PalmCodecs

#endif // WILDPALMS_CODECS_KDE_PIM_CONVERT_H
