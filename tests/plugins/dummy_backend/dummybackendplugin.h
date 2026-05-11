#ifndef WILDPALMS_DUMMYBACKENDPLUGIN_H
#define WILDPALMS_DUMMYBACKENDPLUGIN_H

#include "core/ibackendplugin_v2.h"

#include <memory>
#include <QObject>

class DummyBackendPlugin : public QObject, public WildPalms::IBackendPluginV2
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPluginV2)
public:
    explicit DummyBackendPlugin(QObject *parent = nullptr);

    QString pluginId() const override;
    QString displayName() const override;
    QIcon   icon() const override;
    QString description() const override;
    QString version() const override;

    QStringList claimedDatabases() const override;
    std::unique_ptr<Kalburator::Sync::IBlobBackend>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device) override;
};

#endif // WILDPALMS_DUMMYBACKENDPLUGIN_H
