#ifndef WILDPALMS_PLUGINMETADATAHELPERS_H
#define WILDPALMS_PLUGINMETADATAHELPERS_H

#include <QString>
#include <QStringList>

class KPluginMetaData;

namespace WildPalms::Runtime {

/// Read a custom string value from plugin JSON metadata. Returns an
/// empty QString if the key is absent.
QString metaString(const KPluginMetaData &md, const QString &key);

/// Read a custom bool value. Returns `defaultValue` if the key is
/// absent; case-insensitive "true" is the only affirmative spelling.
bool metaBool(const KPluginMetaData &md, const QString &key, bool defaultValue);

/// Read a custom int value. Returns `defaultValue` if the key is absent
/// or not parseable as an int.
int metaInt(const KPluginMetaData &md, const QString &key, int defaultValue);

/// Read a custom string-array value. Accepts either a JSON array of
/// strings or a single string (returned as a one-element list).
QStringList metaStringList(const KPluginMetaData &md, const QString &key);

} // namespace WildPalms::Runtime

#endif // WILDPALMS_PLUGINMETADATAHELPERS_H
