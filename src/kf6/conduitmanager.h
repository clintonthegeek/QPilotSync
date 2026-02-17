#ifndef CONDUITMANAGER_H
#define CONDUITMANAGER_H

#include <QObject>
#include <QMap>
#include <QList>
#include <QString>
#include <QStringList>

#include <KPluginMetaData>

class IConduit;

/**
 * @brief Discovers, loads, and manages conduit plugins
 *
 * Replaces the hard-coded conduit registration in KF6MainWindow.
 * Conduit plugins are .so files discovered via KPluginMetaData::findPlugins()
 * from the "qpilotsync/conduits" subdirectory.
 *
 * Each plugin's JSON metadata must contain:
 *   - X-QPilotSync-ConduitId   (string)  unique conduit identifier
 *   - X-QPilotSync-DefaultEnabled (bool, default true)
 *   - X-QPilotSync-SortOrder   (int, default 0)   for UI ordering
 *   - X-QPilotSync-RunBefore   (string array)      dependency ordering
 *   - X-QPilotSync-RunAfter    (string array)      dependency ordering
 *
 * Enabled/disabled state is persisted in the application KConfig under
 * the [Conduits] group with keys like "<pluginId>Enabled".
 */
class ConduitManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Per-plugin bookkeeping record
     */
    struct PluginInfo {
        KPluginMetaData metaData;
        IConduit *instance = nullptr;
        bool defaultEnabled = false;
        bool enabled = false;
        int sortOrder = 0;
    };

    explicit ConduitManager(QObject *parent = nullptr);
    ~ConduitManager() override;

    // ========== Discovery ==========

    /**
     * @brief Scan the plugin directory and populate the internal catalogue
     *
     * Calls KPluginMetaData::findPlugins("qpilotsync/conduits").
     * Does NOT load any plugins -- call loadConduit() or loadConfig()
     * to instantiate them.
     */
    void discoverConduits();

    // ========== Loading / Unloading ==========

    /**
     * @brief Load (instantiate) a single conduit plugin
     *
     * Uses KPluginFactory to create the QObject, then dynamic_cast to IConduit*.
     * Emits conduitLoaded() on success.
     *
     * @param pluginId  The X-QPilotSync-ConduitId value
     * @return true on success
     */
    bool loadConduit(const QString &pluginId);

    /**
     * @brief Unload a previously loaded conduit
     *
     * Emits conduitUnloading() before destruction.
     */
    void unloadConduit(const QString &pluginId);

    // ========== Queries ==========

    /** @brief Return the live IConduit* for a loaded plugin, or nullptr */
    IConduit *conduit(const QString &pluginId) const;

    /** @brief Return all currently loaded & enabled conduits */
    QList<IConduit *> enabledConduits() const;

    /** @brief Return the full catalogue (discovered, loaded or not) */
    QList<PluginInfo> conduitList() const;

    /** @brief Return metadata for a single plugin */
    KPluginMetaData conduitMetaData(const QString &pluginId) const;

    // ========== Enable / Disable ==========

    bool isConduitEnabled(const QString &pluginId) const;
    void setConduitEnabled(const QString &pluginId, bool enabled);

    // ========== Ordering ==========

    /**
     * @brief Resolve execution order via topological sort
     *
     * Reads X-QPilotSync-RunBefore / RunAfter from each enabled plugin's
     * metadata and performs Kahn's algorithm.  Falls back to sortOrder
     * for plugins with no dependency edges.
     *
     * @return Ordered list of conduit IDs (enabled only)
     */
    QStringList resolveExecutionOrder() const;

    // ========== Config Persistence ==========

    /**
     * @brief Load enabled/disabled state from KConfig [Conduits] group
     */
    void loadConfig();

    /**
     * @brief Save enabled/disabled state to KConfig [Conduits] group
     */
    void saveConfig();

Q_SIGNALS:
    /** Emitted after a conduit has been successfully loaded */
    void conduitLoaded(IConduit *conduit);

    /** Emitted just before a conduit is about to be unloaded */
    void conduitUnloading(IConduit *conduit);

private:
    /** @brief Read a custom string value from plugin JSON metadata */
    static QString metaValue(const KPluginMetaData &md, const QString &key);

    /** @brief Read a custom bool value from plugin JSON metadata */
    static bool metaBool(const KPluginMetaData &md, const QString &key, bool defaultValue);

    /** @brief Read a custom int value from plugin JSON metadata */
    static int metaInt(const KPluginMetaData &md, const QString &key, int defaultValue);

    /** @brief Read a custom string-array value from plugin JSON metadata */
    static QStringList metaStringList(const KPluginMetaData &md, const QString &key);

    /** @brief Internal catalogue keyed by conduit ID */
    QMap<QString, PluginInfo> m_plugins;
};

#endif // CONDUITMANAGER_H
