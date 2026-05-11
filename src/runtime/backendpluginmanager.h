#ifndef WILDPALMS_BACKENDPLUGINMANAGER_H
#define WILDPALMS_BACKENDPLUGINMANAGER_H

#include <KPluginMetaData>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

namespace Kalburator::Sync {
    class ISyncHost;
    class SyncEngine;
}

class PalmDeviceConnection;

namespace WildPalms {

class IBackendPluginV2;

/**
 * @brief Discovers, loads, and owns IBackendPluginV2 instances.
 *
 * Replaces ConduitManager for the new plugin ABI. ConduitManager keeps
 * running against IConduit plugins under wildpalms/conduits/ until E.16
 * deletes it. BackendPluginManager scans wildpalms/plugins/ and filters
 * by X-WildPalms-PluginType == "backend".
 *
 * Host / device / coordinator are borrowed pointers whose lifetimes the
 * runtime guarantees to exceed the manager's. May be nullptr in tests.
 */
class BackendPluginManager : public QObject
{
    Q_OBJECT
public:
    struct PluginInfo {
        KPluginMetaData   metaData;
        IBackendPluginV2 *instance = nullptr;
        QStringList       claimedDatabases;
        bool              defaultEnabled = true;
        int               sortOrder      = 0;
    };

    explicit BackendPluginManager(Kalburator::Sync::ISyncHost       *host,
                                   PalmDeviceConnection              *device,
                                   Kalburator::Sync::SyncEngine *coordinator,
                                   QObject                           *parent = nullptr);
    ~BackendPluginManager() override;

    /// Scan plugin dirs (default subdir: "wildpalms/plugins"), filter by
    /// X-WildPalms-PluginType == "backend", and populate the catalogue.
    /// Does NOT instantiate plugins — call loadPlugin(id) for that.
    /// Idempotent: re-scanning refreshes metadata but preserves instances.
    void discoverPlugins();

    /// Instantiate a single plugin by id via KPluginFactory. Returns true
    /// on success. Emits pluginLoaded().
    bool loadPlugin(const QString &pluginId);

    /// Instantiate every discovered plugin (subject to defaultEnabled).
    /// Convenience; the runtime may prefer selective loadPlugin() calls.
    void loadPlugins();

    /// Destroy a loaded plugin. Emits pluginUnloading() first.
    void unloadPlugin(const QString &pluginId);

    // ========== Queries ==========
    QList<IBackendPluginV2 *> plugins() const;
    QList<PluginInfo>         catalogue() const;
    IBackendPluginV2         *plugin(const QString &pluginId) const;
    IBackendPluginV2         *pluginForDatabase(const QString &palmDbName) const;

    // ========== Test / customisation seams ==========
    /// Override the plugin subdir scanned by discoverPlugins. Default:
    /// "wildpalms/plugins". Tests point this at a test-only subdir.
    void setPluginSubdir(const QString &subdir);

Q_SIGNALS:
    void pluginLoaded(IBackendPluginV2 *plugin);
    void pluginUnloading(IBackendPluginV2 *plugin);

protected:
    /// Test-only hook: inject a pre-built IBackendPluginV2* into the
    /// catalogue as if KPluginFactory had loaded it. The manager takes
    /// ownership of `instance` via QObject parenting. Returns false if
    /// `pluginId` already has a live instance.
    bool registerInstanceForTest(const QString &pluginId, IBackendPluginV2 *instance);

private:
    QString m_subdir;
    QMap<QString, PluginInfo> m_plugins;

    Kalburator::Sync::ISyncHost       *m_host        = nullptr;
    PalmDeviceConnection              *m_device      = nullptr;
    Kalburator::Sync::SyncEngine *m_coordinator = nullptr;
};

} // namespace WildPalms

#endif // WILDPALMS_BACKENDPLUGINMANAGER_H
