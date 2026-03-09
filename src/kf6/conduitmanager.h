#ifndef CONDUITMANAGER_H
#define CONDUITMANAGER_H

#include <QObject>
#include <QMap>
#include <QList>
#include <QString>
#include <QStringList>

#include <KPluginMetaData>

class IConduit;
class Profile;

/**
 * @brief Discovers, loads, and manages conduit plugins
 *
 * Replaces the hard-coded conduit registration in KF6MainWindow.
 * Conduit plugins are .so files discovered via KPluginMetaData::findPlugins()
 * from the "wildpalms/conduits" subdirectory.
 *
 * Each plugin's JSON metadata must contain:
 *   - X-WildPalms-ConduitId       (string)  unique conduit identifier
 *   - X-WildPalms-PalmCreatorId   (string)  Palm OS 4-char creator ID (e.g. "memo", "addr")
 *   - X-WildPalms-DefaultEnabled  (bool, default true)
 *   - X-WildPalms-SortOrder       (int, default 0)   for UI ordering
 *   - X-WildPalms-RunBefore       (string array)      dependency ordering
 *   - X-WildPalms-RunAfter        (string array)      dependency ordering
 *
 * Which conduits participate in a given sync is determined by the profile,
 * not by ConduitManager.  This class is a pure discovery/loading service.
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
        QString palmCreatorId;     ///< Palm OS 4-char creator ID (empty if not a Palm-app conduit)
        QStringList databaseClaims;    ///< Database names/patterns claimed (from X-WildPalms-PalmDatabases)
        bool defaultEnabled = false;
        int sortOrder = 0;
    };

    explicit ConduitManager(QObject *parent = nullptr);
    ~ConduitManager() override;

    // ========== Discovery ==========

    /**
     * @brief Scan the plugin directory and populate the internal catalogue
     *
     * Calls KPluginMetaData::findPlugins("wildpalms/conduits").
     * Does NOT load any plugins -- call loadConduit() to instantiate them.
     */
    void discoverConduits();

    // ========== Loading / Unloading ==========

    /**
     * @brief Load (instantiate) a single conduit plugin
     *
     * Uses KPluginFactory to create the QObject, then dynamic_cast to IConduit*.
     * Emits conduitLoaded() on success.
     *
     * @param pluginId  The X-WildPalms-ConduitId value
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

    /** @brief Return the full catalogue (discovered, loaded or not) */
    QList<PluginInfo> conduitList() const;

    /** @brief Return metadata for a single plugin */
    KPluginMetaData conduitMetaData(const QString &pluginId) const;

    /** @brief Return the Palm creator ID for a conduit (empty if none) */
    QString palmCreatorId(const QString &pluginId) const;

    // ========== Database Claim System ==========

    /** @brief Return database name -> list of conduit IDs that claim it */
    QMap<QString, QStringList> databaseClaimMap() const;

    /** @brief Return the active conduit ID for a database, consulting the profile */
    QString activeConduitForDatabase(const QString &dbName, const Profile *profile) const;

    /** @brief Return which databases a conduit is active for */
    QStringList activeDatabasesForConduit(const QString &conduitId, const Profile *profile) const;

    /** @brief Check if a conduit has any database claims */
    bool hasDatabaseClaims(const QString &conduitId) const;

    /** @brief Get claim description for a conduit's database claim */
    QString claimDescription(const QString &conduitId, const QString &dbName) const;

    // ========== Ordering ==========

    /**
     * @brief Resolve execution order via topological sort
     *
     * Reads X-WildPalms-RunBefore / RunAfter from each enabled plugin's
     * metadata and performs Kahn's algorithm.  Falls back to sortOrder
     * for plugins with no dependency edges.
     *
     * @return Ordered list of conduit IDs (enabled only)
     */
    QStringList resolveExecutionOrder(const QStringList &enabledConduitIds,
                                       const Profile *profile = nullptr) const;

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
