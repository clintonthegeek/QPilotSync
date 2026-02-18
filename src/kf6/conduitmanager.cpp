#include "conduitmanager.h"
#include "../core/iconduit.h"

#include <KPluginFactory>
#include <KSharedConfig>
#include <KConfigGroup>

#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

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
        info.defaultEnabled = metaBool(md, QStringLiteral("X-WildPalms-DefaultEnabled"), true);
        info.enabled        = info.defaultEnabled;
        info.sortOrder      = metaInt(md, QStringLiteral("X-WildPalms-SortOrder"), 0);

        m_plugins.insert(conduitId, info);

        qDebug() << "[ConduitManager] Discovered conduit:" << conduitId
                 << "creatorId:" << (info.palmCreatorId.isEmpty()
                                      ? QStringLiteral("(none)") : info.palmCreatorId)
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

QList<IConduit *> ConduitManager::enabledConduits() const
{
    QList<IConduit *> result;
    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        if (it->enabled && it->instance) {
            result.append(it->instance);
        }
    }
    return result;
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

// ========== Enable / Disable ==========

bool ConduitManager::isConduitEnabled(const QString &pluginId) const
{
    auto it = m_plugins.constFind(pluginId);
    if (it != m_plugins.constEnd()) {
        return it->enabled;
    }
    return false;
}

void ConduitManager::setConduitEnabled(const QString &pluginId, bool enabled)
{
    if (!m_plugins.contains(pluginId)) {
        return;
    }

    if (enabled) {
        // Enforce one-active-per-creator-ID: if another conduit is already
        // enabled for the same Palm creator ID, disable it first.
        const QString creatorId = m_plugins[pluginId].palmCreatorId;
        if (!creatorId.isEmpty()) {
            for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
                if (it.key() != pluginId
                    && it->palmCreatorId == creatorId
                    && it->enabled) {
                    qDebug() << "[ConduitManager] Disabling" << it.key()
                             << "— creator ID" << creatorId
                             << "claimed by" << pluginId;
                    it->enabled = false;
                }
            }
        }
    }

    m_plugins[pluginId].enabled = enabled;
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

QString ConduitManager::enabledConduitForCreatorId(const QString &creatorId) const
{
    if (creatorId.isEmpty()) {
        return QString();
    }
    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        if (it->enabled && it->palmCreatorId == creatorId) {
            return it.key();
        }
    }
    return QString();
}

// ========== Ordering ==========

QStringList ConduitManager::resolveExecutionOrder() const
{
    // Collect enabled conduit IDs
    QStringList conduitIds;
    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        if (it->enabled) {
            conduitIds.append(it.key());
        }
    }

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
        for (const QString &beforeId : before) {
            if (conduitIds.contains(beforeId)) {
                mustRunBefore[id].append(beforeId);
                inDegree[beforeId]++;
            }
        }

        // "I must run after X"  ->  edge: X -> id
        const QStringList after =
            metaStringList(info.metaData, QStringLiteral("X-WildPalms-RunAfter"));
        for (const QString &afterId : after) {
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

// ========== Config Persistence ==========

void ConduitManager::loadConfig()
{
    KConfigGroup grp(KSharedConfig::openConfig(), QStringLiteral("Conduits"));

    for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
        const QString key = it.key() + QStringLiteral("Enabled");
        // If the key is absent in config, fall back to the plugin's default
        it->enabled = grp.readEntry(key, it->defaultEnabled);
    }
}

void ConduitManager::saveConfig()
{
    KConfigGroup grp(KSharedConfig::openConfig(), QStringLiteral("Conduits"));

    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        const QString key = it.key() + QStringLiteral("Enabled");
        grp.writeEntry(key, it->enabled);
    }

    grp.sync();
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
