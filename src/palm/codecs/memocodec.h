#ifndef WILDPALMS_CODECS_MEMOCODEC_H
#define WILDPALMS_CODECS_MEMOCODEC_H

#include <optional>

#include <QByteArray>
#include <QString>

namespace WildPalms::PalmCodecs {

/// Palm Memo content POD. The enclosing PalmRecord carries recordId,
/// category slot, and the isDeleted/isDirty attribute bits; this
/// struct is content-only.
struct Memo {
    QString text;
    bool    isPrivate = false;

    bool operator==(const Memo &) const = default;
};

/// Encode a Memo to Palm-wire bytes. Calls pisock's `pack_Memo`. The
/// result is what goes into `PalmRecord::data`. Does not encode
/// isPrivate — that is a PalmRecord.attributes concern.
QByteArray encodeMemo(const Memo &memo);

/// Decode Palm-wire bytes to a Memo. Returns nullopt only on an
/// unrecoverable pisock failure. Empty input returns a valid Memo
/// with an empty text string (Palm sometimes stores empty memos).
std::optional<Memo> decodeMemo(QByteArrayView bytes);

} // namespace WildPalms::PalmCodecs

#endif // WILDPALMS_CODECS_MEMOCODEC_H
