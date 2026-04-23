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

bool BackendPluginManager::loadPlugin(const QString &) { return false; }
void BackendPluginManager::loadPlugins() {}
void BackendPluginManager::unloadPlugin(const QString &) {}

QList<IBackendPlugin *> BackendPluginManager::plugins() const { return {}; }
QList<BackendPluginManager::PluginInfo> BackendPluginManager::catalogue() const { return m_plugins.values(); }
IBackendPlugin *BackendPluginManager::plugin(const QString &) const { return nullptr; }
IBackendPlugin *BackendPluginManager::pluginForDatabase(const QString &) const { return nullptr; }

} // namespace WildPalms
