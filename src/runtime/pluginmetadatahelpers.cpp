#include "pluginmetadatahelpers.h"

#include <KPluginMetaData>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace WildPalms::Runtime {

QString metaString(const KPluginMetaData &md, const QString &key)
{
    return md.value(key);
}

bool metaBool(const KPluginMetaData &md, const QString &key, bool defaultValue)
{
    const QString val = md.value(key);
    if (val.isEmpty()) {
        return defaultValue;
    }
    return val.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

int metaInt(const KPluginMetaData &md, const QString &key, int defaultValue)
{
    const QString val = md.value(key);
    if (val.isEmpty()) {
        return defaultValue;
    }
    bool ok = false;
    const int v = val.toInt(&ok);
    return ok ? v : defaultValue;
}

QStringList metaStringList(const KPluginMetaData &md, const QString &key)
{
    QStringList result;
    const QJsonObject raw = md.rawData();
    const QJsonValue val = raw.value(key);

    if (val.isArray()) {
        const QJsonArray arr = val.toArray();
        result.reserve(arr.size());
        for (const QJsonValue &v : arr) {
            const QString s = v.toString();
            if (!s.isEmpty()) {
                result.append(s);
            }
        }
    } else if (val.isString()) {
        const QString s = val.toString();
        if (!s.isEmpty()) {
            result.append(s);
        }
    }

    return result;
}

} // namespace WildPalms::Runtime
