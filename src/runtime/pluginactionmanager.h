#ifndef WILDPALMS_PLUGINACTIONMANAGER_H
#define WILDPALMS_PLUGINACTIONMANAGER_H

#include <KPluginMetaData>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

class PalmDeviceConnection;

namespace WildPalms {

class IPluginAction;

/**
 * @brief Discovers, loads, and exposes IPluginAction plugins.
 *
 * Sister manager to BackendPluginManager. Shares the
 * wildpalms/plugins/ discovery subdir; filters by
 * X-WildPalms-PluginType == "action".
 */
class PluginActionManager : public QObject
{
    Q_OBJECT
public:
    struct ActionInfo {
        KPluginMetaData  metaData;
        IPluginAction   *instance = nullptr;
    };

    explicit PluginActionManager(PalmDeviceConnection *device,
                                  QObject              *parent = nullptr);
    ~PluginActionManager() override;

    void discoverActions();
    bool loadAction(const QString &pluginId);
    void loadActions();
    void unloadAction(const QString &pluginId);

    QList<IPluginAction *> actions() const;
    IPluginAction         *action(const QString &pluginId) const;

    void setPluginSubdir(const QString &subdir);

Q_SIGNALS:
    void actionLoaded(IPluginAction *action);
    void actionUnloading(IPluginAction *action);

protected:
    bool registerInstanceForTest(const QString &pluginId, IPluginAction *instance);

private:
    QString m_subdir;
    QMap<QString, ActionInfo> m_actions;
    PalmDeviceConnection *m_device = nullptr;
};

} // namespace WildPalms

#endif // WILDPALMS_PLUGINACTIONMANAGER_H
