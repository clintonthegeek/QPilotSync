#ifndef WILDPALMS_CODECS_PALMTEXT_H
#define WILDPALMS_CODECS_PALMTEXT_H

#include <QByteArray>
#include <QString>

namespace WildPalms::PalmCodecs {

/// Decode a null-terminated Palm text buffer (Windows-1252) into QString.
/// Returns an empty string if `palmText` is null.
QString decodePalmText(const char *palmText);

/// Encode a QString to Windows-1252 for Palm. Characters that have no
/// Windows-1252 representation are replaced with '?'. The returned
/// QByteArray is NOT null-terminated — the caller appends '\0' when
/// building a pi_buffer payload.
QByteArray encodePalmText(const QString &text);

} // namespace WildPalms::PalmCodecs

#endif // WILDPALMS_CODECS_PALMTEXT_H
