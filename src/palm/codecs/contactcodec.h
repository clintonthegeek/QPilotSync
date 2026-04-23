#ifndef WILDPALMS_CODECS_CONTACTCODEC_H
#define WILDPALMS_CODECS_CONTACTCODEC_H

#include <array>
#include <optional>

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace WildPalms::PalmCodecs {

/// Palm Address record content POD. The enclosing PalmRecord carries
/// recordId, category slot, and the isDeleted/isDirty/isSecret
/// attribute bits; this struct is content-only.
struct Contact {
    QString lastName;
    QString firstName;
    QString company;
    QString title;

    /// Five Palm phone slots. Slot labels are in `phoneLabels`.
    std::array<QString, 5> phone {};

    /// Phone slot labels — one per **non-empty** `phone` slot, in
    /// matching order. Size ranges 0..5. Empty-phone slots carry no
    /// label (so `Contact{}` round-trips with an empty list rather
    /// than five defaults). Typical entries: Work, Home, Fax, Other,
    /// E-mail, Main, Pager, Mobile. The Palm device stores these as
    /// per-record small integers indexing into the AddressAppInfo's
    /// label table; the codec resolves them to strings on decode and
    /// reverses on encode.
    QStringList phoneLabels;

    /// Which phone slot (0..4) is the "preferred" one for UI display.
    int showPhone = 0;

    QString address;
    QString city;
    QString state;
    QString zip;
    QString country;

    /// Four Palm custom fields. Labels live on AddressAppInfo and are
    /// not exposed by this codec (appearance defer to E.10/E.17).
    std::array<QString, 4> custom {};

    QString note;

    /// Convenience flag. NOT encoded — PalmRecord.attributes owns the
    /// Secret bit. Adapter layer sets this when surfacing to callers.
    bool isPrivate = false;

    bool operator==(const Contact &) const = default;
};

/// Encode a Contact to Palm-wire bytes via `pack_Address`.
QByteArray encodeContact(const Contact &c);

/// Decode Palm-wire bytes to a Contact via `unpack_Address`. Returns
/// nullopt on malformed input (pisock returns negative). Empty input
/// returns nullopt — callers should not feed empty data.
std::optional<Contact> decodeContact(QByteArrayView bytes);

} // namespace WildPalms::PalmCodecs

#endif // WILDPALMS_CODECS_CONTACTCODEC_H
