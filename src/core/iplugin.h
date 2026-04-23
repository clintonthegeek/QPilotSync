#ifndef WILDPALMS_IPLUGIN_H
#define WILDPALMS_IPLUGIN_H

#include <QIcon>
#include <QJsonObject>
#include <QString>
#include <QWidget>

namespace WildPalms {

/**
 * @brief Base metadata-only plugin interface for WildPalms.
 *
 * Pure abstract C++ class (not a QObject) so concrete plugin classes
 * can multi-inherit QObject + IPlugin without diamond inheritance.
 *
 * Concrete plugin *kinds* extend this:
 *   - IBackendPlugin : IPlugin — syncs records via libkalburator backends.
 *   - IPluginAction  : IPlugin — one-shot triggerable operation (Install,
 *                                Restore, etc.), no record-level sync.
 *
 * Discriminated at load time by the plugin manifest key
 * `X-WildPalms-PluginType` (values: "backend" | "action").
 *
 * Replaces IConduit (Phase E.8). Old IConduit surface stays alive until
 * E.16 so the ABI rewrite can land incrementally.
 */
class IPlugin
{
public:
    virtual ~IPlugin() = default;

    // ========== Identity ==========
    virtual QString pluginId() const = 0;
    virtual QString displayName() const = 0;
    virtual QIcon   icon() const = 0;
    virtual QString description() const = 0;
    virtual QString version() const = 0;

    // ========== Optional configuration surface ==========
    virtual bool     hasSettings() const { return false; }
    virtual QWidget *createSettingsWidget(QWidget *parent)
    {
        Q_UNUSED(parent)
        return nullptr;
    }
    virtual void        loadSettings(const QJsonObject &settings) { Q_UNUSED(settings) }
    virtual QJsonObject saveSettings() const { return {}; }
};

} // namespace WildPalms

Q_DECLARE_INTERFACE(WildPalms::IPlugin, "ca.vibekoder.WildPalms.IPlugin/1.0")

#endif // WILDPALMS_IPLUGIN_H
