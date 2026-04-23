#include "backendpluginmanager.h"

#include "core/ibackendplugin.h"
#include "pluginmetadatahelpers.h"

#include <KPluginFactory>

#include <QDebug>

namespace WildPalms {

BackendPluginManager::BackendPluginManager(Kalburator::Sync::ISyncHost       *host,
                                             PalmDeviceConnection              *device,
                                             Kalburator::Sync::SyncCoordinator *coordinator,
                                             QObject                           *parent)
    : QObject(parent)
    , m_subdir(QStringLiteral("wildpalms/plugins"))
    , m_host(host)
    , m_device(device)
    , m_coordinator(coordinator)
{
}

BackendPluginManager::~BackendPluginManager()
{
    const QStringList ids = m_plugins.keys();
    for (const QString &id : ids) {
        if (m_plugins[id].instance) {
            unloadPlugin(id);
        }
    }
}

void BackendPluginManager::setPluginSubdir(const QString &subdir) { m_subdir = subdir; }

void BackendPluginManager::discoverPlugins()
{
    const QList<KPluginMetaData> found =
        KPluginMetaData::findPlugins(m_subdir);

    for (const KPluginMetaData &md : found) {
        // Filter: only keep plugins declaring themselves as "backend".
        const QString pluginType =
            Runtime::metaString(md, QStringLiteral("X-WildPalms-PluginType"));
        if (pluginType != QStringLiteral("backend")) {
            continue;
        }

        QString pluginId = md.pluginId();
        if (pluginId.isEmpty()) {
            qWarning() << "[BackendPluginManager] Skipping plugin with empty id:"
                       << md.fileName();
            continue;
        }

        // Re-discovery: preserve running instance, refresh metadata.
        if (m_plugins.contains(pluginId)) {
            m_plugins[pluginId].metaData = md;
            continue;
        }

        PluginInfo info;
        info.metaData        = md;
        info.instance        = nullptr;
        info.claimedDatabases = Runtime::metaStringList(
            md, QStringLiteral("X-WildPalms-PalmDatabases"));
        info.defaultEnabled  = Runtime::metaBool(
            md, QStringLiteral("X-WildPalms-DefaultEnabled"), true);
        info.sortOrder       = Runtime::metaInt(
            md, QStringLiteral("X-WildPalms-SortOrder"), 0);

        m_plugins.insert(pluginId, info);

        qDebug() << "[BackendPluginManager] Discovered backend plugin:" << pluginId
                 << "databases:" << info.claimedDatabases
                 << "sortOrder:" << info.sortOrder;
    }
}

bool BackendPluginManager::loadPlugin(const QString &pluginId)
{
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end()) {
        qWarning() << "[BackendPluginManager] Unknown plugin:" << pluginId;
        return false;
    }
    if (it->instance) {
        return true; // already loaded
    }

    auto factoryResult = KPluginFactory::loadFactory(it->metaData);
    if (!factoryResult) {
        qWarning() << "[BackendPluginManager] Factory load failed:" << pluginId
                   << factoryResult.errorString;
        return false;
    }

    QObject *obj = factoryResult.plugin->create<QObject>(this);
    if (!obj) {
        qWarning() << "[BackendPluginManager] Factory returned nullptr:" << pluginId;
        return false;
    }

    auto *plug = dynamic_cast<IBackendPlugin *>(obj);
    if (!plug) {
        qWarning() << "[BackendPluginManager] Plugin does not implement IBackendPlugin:"
                   << pluginId;
        delete obj;
        return false;
    }

    it->instance = plug;
    qDebug() << "[BackendPluginManager] Loaded plugin:" << plug->pluginId()
             << "(" << plug->displayName() << ")";
    emit pluginLoaded(plug);
    return true;
}

void BackendPluginManager::loadPlugins()
{
    const QStringList ids = m_plugins.keys();
    for (const QString &id : ids) {
        if (m_plugins[id].defaultEnabled && !m_plugins[id].instance) {
            loadPlugin(id);
        }
    }
}

void BackendPluginManager::unloadPlugin(const QString &pluginId)
{
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end() || !it->instance) {
        return;
    }
    emit pluginUnloading(it->instance);
    QObject *obj = dynamic_cast<QObject *>(it->instance);
    delete obj;
    it->instance = nullptr;
    qDebug() << "[BackendPluginManager] Unloaded plugin:" << pluginId;
}

QList<IBackendPlugin *> BackendPluginManager::plugins() const
{
    QList<IBackendPlugin *> out;
    for (const PluginInfo &info : m_plugins) {
        if (info.instance) {
            out.append(info.instance);
        }
    }
    return out;
}

QList<BackendPluginManager::PluginInfo> BackendPluginManager::catalogue() const { return m_plugins.values(); }

IBackendPlugin *BackendPluginManager::plugin(const QString &pluginId) const
{
    auto it = m_plugins.constFind(pluginId);
    return (it != m_plugins.constEnd()) ? it->instance : nullptr;
}

IBackendPlugin *BackendPluginManager::pluginForDatabase(const QString &palmDbName) const
{
    for (const PluginInfo &info : m_plugins) {
        if (info.claimedDatabases.contains(palmDbName) && info.instance) {
            return info.instance;
        }
    }
    return nullptr;
}

bool BackendPluginManager::registerInstanceForTest(const QString &pluginId,
                                                     IBackendPlugin *instance)
{
    if (!instance) return false;

    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end()) {
        // Synthesise a PluginInfo with minimal metadata so lifecycle
        // and queries work. claimedDatabases pulled from the instance.
        PluginInfo info;
        info.instance         = instance;
        info.claimedDatabases = instance->claimedDatabases();
        info.defaultEnabled   = true;
        info.sortOrder        = 0;
        m_plugins.insert(pluginId, info);
    } else {
        if (it->instance) return false;
        it->instance = instance;
        if (it->claimedDatabases.isEmpty()) {
            it->claimedDatabases = instance->claimedDatabases();
        }
    }

    if (auto *obj = dynamic_cast<QObject *>(instance)) {
        obj->setParent(this);
    }
    emit pluginLoaded(instance);
    return true;
}

} // namespace WildPalms
