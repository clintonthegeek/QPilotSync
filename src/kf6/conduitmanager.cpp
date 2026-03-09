#include "conduitmanager.h"
#include "../core/iconduit.h"
#include "../profile.h"

#include <KPluginFactory>

#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QRegularExpression>

// ========== Construction / Destruction ==========

ConduitManager::ConduitManager(QObject *parent)
    : QObject(parent)
{
}

ConduitManager::~ConduitManager()
{
    // Unload all loaded conduits (emits conduitUnloading for each)
    const QStringList ids = m_plugins.keys();
    for (const QString &id : ids) {
        if (m_plugins[id].instance) {
            unloadConduit(id);
        }
    }
}

// ========== Discovery ==========

void ConduitManager::discoverConduits()
{
    const QList<KPluginMetaData> found =
        KPluginMetaData::findPlugins(QStringLiteral("wildpalms/conduits"));

    for (const KPluginMetaData &md : found) {
        QString conduitId = metaValue(md, QStringLiteral("X-WildPalms-ConduitId"));

        // Fall back to the plugin's internal name if the custom key is absent
        if (conduitId.isEmpty()) {
            conduitId = md.pluginId();
        }

        if (conduitId.isEmpty()) {
            qWarning() << "[ConduitManager] Skipping plugin with no conduit ID:"
                       << md.fileName();
            continue;
        }

        // If we already know about this ID (e.g. re-discovery), update metadata
        // but preserve runtime state.
        if (m_plugins.contains(conduitId)) {
            m_plugins[conduitId].metaData = md;
            continue;
        }

        PluginInfo info;
        info.metaData       = md;
        info.instance       = nullptr;
        info.palmCreatorId  = metaValue(md, QStringLiteral("X-WildPalms-PalmCreatorId"));
        info.databaseClaims = metaStringList(md, QStringLiteral("X-WildPalms-PalmDatabases"));
        info.defaultEnabled = metaBool(md, QStringLiteral("X-WildPalms-DefaultEnabled"), true);
        info.sortOrder      = metaInt(md, QStringLiteral("X-WildPalms-SortOrder"), 0);

        m_plugins.insert(conduitId, info);

        qDebug() << "[ConduitManager] Discovered conduit:" << conduitId
                 << "creatorId:" << (info.palmCreatorId.isEmpty()
                                      ? QStringLiteral("(none)") : info.palmCreatorId)
                 << "databases:" << info.databaseClaims
                 << "sortOrder:" << info.sortOrder
                 << "defaultEnabled:" << info.defaultEnabled;
    }
}

// ========== Loading / Unloading ==========

bool ConduitManager::loadConduit(const QString &pluginId)
{
    if (!m_plugins.contains(pluginId)) {
        qWarning() << "[ConduitManager] Unknown conduit ID:" << pluginId;
        return false;
    }

    PluginInfo &info = m_plugins[pluginId];

    // Already loaded?
    if (info.instance) {
        return true;
    }

    // Use KPluginFactory to instantiate the plugin
    auto factoryResult = KPluginFactory::loadFactory(info.metaData);
    if (!factoryResult) {
        qWarning() << "[ConduitManager] Failed to load factory for" << pluginId
                   << ":" << factoryResult.errorString;
        return false;
    }

    QObject *obj = factoryResult.plugin->create<QObject>(this);
    if (!obj) {
        qWarning() << "[ConduitManager] Factory returned nullptr for" << pluginId;
        return false;
    }

    // The plugin class inherits both QObject and IConduit,
    // so dynamic_cast works here (requires RTTI).
    IConduit *conduit = dynamic_cast<IConduit *>(obj);
    if (!conduit) {
        qWarning() << "[ConduitManager] Plugin" << pluginId
                   << "does not implement IConduit -- deleting";
        delete obj;
        return false;
    }

    info.instance = conduit;

    qDebug() << "[ConduitManager] Loaded conduit:" << conduit->conduitId()
             << "(" << conduit->displayName() << ")";

    emit conduitLoaded(conduit);
    return true;
}

void ConduitManager::unloadConduit(const QString &pluginId)
{
    if (!m_plugins.contains(pluginId)) {
        return;
    }

    PluginInfo &info = m_plugins[pluginId];
    if (!info.instance) {
        return;
    }

    emit conduitUnloading(info.instance);

    // The instance is a QObject* parented to this ConduitManager,
    // but we delete explicitly to control ordering.
    // dynamic_cast back to QObject* for safe delete.
    QObject *obj = dynamic_cast<QObject *>(info.instance);
    delete obj;

    info.instance = nullptr;

    qDebug() << "[ConduitManager] Unloaded conduit:" << pluginId;
}

// ========== Queries ==========

IConduit *ConduitManager::conduit(const QString &pluginId) const
{
    auto it = m_plugins.constFind(pluginId);
    if (it != m_plugins.constEnd()) {
        return it->instance;
    }
    return nullptr;
}

QList<ConduitManager::PluginInfo> ConduitManager::conduitList() const
{
    return m_plugins.values();
}

KPluginMetaData ConduitManager::conduitMetaData(const QString &pluginId) const
{
    auto it = m_plugins.constFind(pluginId);
    if (it != m_plugins.constEnd()) {
        return it->metaData;
    }
    return KPluginMetaData();
}

// ========== Creator ID Queries ==========

QString ConduitManager::palmCreatorId(const QString &pluginId) const
{
    auto it = m_plugins.constFind(pluginId);
    if (it != m_plugins.constEnd()) {
        return it->palmCreatorId;
    }
    return QString();
}

// ========== Database Claim System ==========

QMap<QString, QStringList> ConduitManager::databaseClaimMap() const
{
    QMap<QString, QStringList> map;
    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        for (const QString &claim : it->databaseClaims) {
            map[claim].append(it.key());
        }
    }
    return map;
}

bool ConduitManager::hasDatabaseClaims(const QString &conduitId) const
{
    auto it = m_plugins.constFind(conduitId);
    if (it != m_plugins.constEnd()) {
        return !it->databaseClaims.isEmpty();
    }
    return false;
}

QString ConduitManager::activeConduitForDatabase(const QString &dbName, const Profile *profile) const
{
    if (!profile) return QString();

    // Check profile's explicit selection
    QString selected = profile->activeDatabaseHandler(dbName);
    if (!selected.isEmpty() && m_plugins.contains(selected)) {
        return selected;
    }

    // Auto-select if only one conduit claims this database
    QStringList claimants;
    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        for (const QString &claim : it->databaseClaims) {
            if (claim == dbName) {
                claimants.append(it.key());
            } else if (claim.contains(QLatin1Char('*')) || claim.contains(QLatin1Char('?'))) {
                QRegularExpression re(QRegularExpression::wildcardToRegularExpression(claim));
                if (re.match(dbName).hasMatch()) {
                    claimants.append(it.key());
                }
            }
        }
    }

    if (claimants.size() == 1) {
        return claimants.first();
    }

    return QString();
}

QStringList ConduitManager::activeDatabasesForConduit(const QString &conduitId, const Profile *profile) const
{
    auto it = m_plugins.constFind(conduitId);
    if (it == m_plugins.constEnd()) return {};

    QStringList active;
    for (const QString &claim : it->databaseClaims) {
        if (activeConduitForDatabase(claim, profile) == conduitId) {
            active.append(claim);
        }
    }
    return active;
}

QString ConduitManager::claimDescription(const QString &conduitId, const QString &dbName) const
{
    auto it = m_plugins.constFind(conduitId);
    if (it == m_plugins.constEnd()) return QString();

    const QJsonObject raw = it->metaData.rawData();
    const QJsonObject descriptions = raw.value(QStringLiteral("X-WildPalms-ClaimDescriptions")).toObject();
    QString desc = descriptions.value(dbName).toString();
    if (desc.isEmpty()) {
        desc = it->metaData.description();
    }
    return desc;
}

// ========== Ordering ==========

QStringList ConduitManager::resolveExecutionOrder(const QStringList &enabledConduitIds,
                                                   const Profile *profile) const
{
    QStringList conduitIds = enabledConduitIds;

    if (conduitIds.isEmpty()) {
        return conduitIds;
    }

    // Build dependency graph  (edge A -> B means "A must run before B")
    QMap<QString, QStringList> mustRunBefore;
    QMap<QString, int> inDegree;

    for (const QString &id : conduitIds) {
        inDegree[id] = 0;
        mustRunBefore[id] = QStringList();
    }

    for (const QString &id : conduitIds) {
        const PluginInfo &info = m_plugins[id];

        // "I must run before X"  ->  edge: id -> X
        const QStringList before =
            metaStringList(info.metaData, QStringLiteral("X-WildPalms-RunBefore"));
        for (const QString &rawRef : before) {
            QString beforeId = rawRef;
            if (rawRef.startsWith(QLatin1Char('@')) && profile) {
                beforeId = activeConduitForDatabase(rawRef.mid(1), profile);
                if (beforeId.isEmpty()) continue;
            }
            if (conduitIds.contains(beforeId)) {
                mustRunBefore[id].append(beforeId);
                inDegree[beforeId]++;
            }
        }

        // "I must run after X"  ->  edge: X -> id
        const QStringList after =
            metaStringList(info.metaData, QStringLiteral("X-WildPalms-RunAfter"));
        for (const QString &rawRef : after) {
            QString afterId = rawRef;
            if (rawRef.startsWith(QLatin1Char('@')) && profile) {
                afterId = activeConduitForDatabase(rawRef.mid(1), profile);
                if (afterId.isEmpty()) continue;
            }
            if (conduitIds.contains(afterId)) {
                mustRunBefore[afterId].append(id);
                inDegree[id]++;
            }
        }
    }

    // Kahn's algorithm -- topological sort
    // When choosing among zero-in-degree nodes, prefer lower sortOrder
    // (with alphabetical tiebreak for determinism).
    QStringList result;
    QStringList queue;

    for (const QString &id : conduitIds) {
        if (inDegree[id] == 0) {
            queue.append(id);
        }
    }

    // Sort helper: compare by (sortOrder, id)
    auto cmp = [this](const QString &a, const QString &b) -> bool {
        int sa = m_plugins.contains(a) ? m_plugins[a].sortOrder : 0;
        int sb = m_plugins.contains(b) ? m_plugins[b].sortOrder : 0;
        if (sa != sb) return sa < sb;
        return a < b;
    };

    while (!queue.isEmpty()) {
        std::sort(queue.begin(), queue.end(), cmp);

        QString current = queue.takeFirst();
        result.append(current);

        for (const QString &next : mustRunBefore[current]) {
            inDegree[next]--;
            if (inDegree[next] == 0) {
                queue.append(next);
            }
        }
    }

    // If we did not visit all nodes there is a cycle.
    // Append the remaining ones so we still return something usable.
    if (result.size() != conduitIds.size()) {
        qWarning() << "[ConduitManager] Circular dependency detected --"
                   << "appending remaining conduits in arbitrary order";
        for (const QString &id : conduitIds) {
            if (!result.contains(id)) {
                result.append(id);
            }
        }
    }

    return result;
}

// ========== Private Helpers ==========

QString ConduitManager::metaValue(const KPluginMetaData &md, const QString &key)
{
    return md.value(key);
}

bool ConduitManager::metaBool(const KPluginMetaData &md, const QString &key, bool defaultValue)
{
    const QString val = md.value(key);
    if (val.isEmpty()) {
        return defaultValue;
    }
    return (val.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
}

int ConduitManager::metaInt(const KPluginMetaData &md, const QString &key, int defaultValue)
{
    const QString val = md.value(key);
    if (val.isEmpty()) {
        return defaultValue;
    }
    bool ok = false;
    int v = val.toInt(&ok);
    return ok ? v : defaultValue;
}

QStringList ConduitManager::metaStringList(const KPluginMetaData &md, const QString &key)
{
    QStringList result;

    // rawData() returns the full QJsonObject from the plugin's metadata.
    // Array values (like RunBefore/RunAfter) must be read from there.
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
        // Accept a single string as a one-element list
        const QString s = val.toString();
        if (!s.isEmpty()) {
            result.append(s);
        }
    }

    return result;
}
