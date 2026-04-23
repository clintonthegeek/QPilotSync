#ifndef WILDPALMS_CODECS_TODOCODEC_H
#define WILDPALMS_CODECS_TODOCODEC_H

#include <optional>

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace WildPalms::PalmCodecs {

/// Palm ToDo record content POD. Priority is Palm-native 1..5 (1 = highest).
struct Todo {
    QString   description;
    QString   note;
    bool      hasIndefiniteDue = true;
    QDateTime due;             ///< valid when !hasIndefiniteDue
    int       priority = 1;    ///< 1..5
    bool      isComplete = false;
    bool      isPrivate = false;

    bool operator==(const Todo &) const = default;
};

/// Encode a Todo via pisock's `pack_ToDo`.
QByteArray encodeTodo(const Todo &todo);

/// Decode via `unpack_ToDo`. Empty input returns nullopt.
std::optional<Todo> decodeTodo(QByteArrayView bytes);

} // namespace WildPalms::PalmCodecs

#endif // WILDPALMS_CODECS_TODOCODEC_H
