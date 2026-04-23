#include "pluginactionmanager.h"

#include "core/ipluginaction.h"
#include "pluginmetadatahelpers.h"

#include <KPluginFactory>

#include <QDebug>

namespace WildPalms {

PluginActionManager::PluginActionManager(PalmDeviceConnection *device, QObject *parent)
    : QObject(parent)
    , m_subdir(QStringLiteral("wildpalms/plugins"))
    , m_device(device)
{
}

PluginActionManager::~PluginActionManager()
{
    const QStringList ids = m_actions.keys();
    for (const QString &id : ids) {
        if (m_actions[id].instance) unloadAction(id);
    }
}

void PluginActionManager::setPluginSubdir(const QString &subdir) { m_subdir = subdir; }

void PluginActionManager::discoverActions()
{
    const QList<KPluginMetaData> found = KPluginMetaData::findPlugins(m_subdir);
    for (const KPluginMetaData &md : found) {
        const QString type = Runtime::metaString(md, QStringLiteral("X-WildPalms-PluginType"));
        if (type != QStringLiteral("action")) continue;

        const QString id = md.pluginId();
        if (id.isEmpty()) continue;

        if (m_actions.contains(id)) {
            m_actions[id].metaData = md;
            continue;
        }
        ActionInfo info;
        info.metaData = md;
        m_actions.insert(id, info);
        qDebug() << "[PluginActionManager] Discovered action:" << id;
    }
}

bool PluginActionManager::loadAction(const QString &pluginId)
{
    auto it = m_actions.find(pluginId);
    if (it == m_actions.end()) return false;
    if (it->instance) return true;

    auto factoryResult = KPluginFactory::loadFactory(it->metaData);
    if (!factoryResult) {
        qWarning() << "[PluginActionManager] Factory load failed:" << pluginId
                   << factoryResult.errorString;
        return false;
    }
    QObject *obj = factoryResult.plugin->create<QObject>(this);
    if (!obj) return false;

    auto *action = dynamic_cast<IPluginAction *>(obj);
    if (!action) {
        delete obj;
        return false;
    }

    it->instance = action;
    emit actionLoaded(action);
    return true;
}

void PluginActionManager::loadActions()
{
    const QStringList ids = m_actions.keys();
    for (const QString &id : ids) {
        if (!m_actions[id].instance) loadAction(id);
    }
}

void PluginActionManager::unloadAction(const QString &pluginId)
{
    auto it = m_actions.find(pluginId);
    if (it == m_actions.end() || !it->instance) return;
    emit actionUnloading(it->instance);
    delete dynamic_cast<QObject *>(it->instance);
    it->instance = nullptr;
}

QList<IPluginAction *> PluginActionManager::actions() const
{
    QList<IPluginAction *> out;
    for (const ActionInfo &info : m_actions) {
        if (info.instance) out.append(info.instance);
    }
    return out;
}

IPluginAction *PluginActionManager::action(const QString &pluginId) const
{
    auto it = m_actions.constFind(pluginId);
    return (it != m_actions.constEnd()) ? it->instance : nullptr;
}

bool PluginActionManager::registerInstanceForTest(const QString &pluginId,
                                                    IPluginAction *instance)
{
    if (!instance) return false;
    auto it = m_actions.find(pluginId);
    if (it != m_actions.end() && it->instance) return false;
    if (it == m_actions.end()) {
        ActionInfo info;
        info.instance = instance;
        m_actions.insert(pluginId, info);
    } else {
        it->instance = instance;
    }
    if (auto *obj = dynamic_cast<QObject *>(instance)) obj->setParent(this);
    emit actionLoaded(instance);
    return true;
}

} // namespace WildPalms
