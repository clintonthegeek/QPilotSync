#ifndef WILDPALMS_DUMMYBACKENDPLUGIN_H
#define WILDPALMS_DUMMYBACKENDPLUGIN_H

#include "core/ibackendplugin.h"

#include <QObject>

class DummyBackendPlugin : public QObject, public WildPalms::IBackendPlugin
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPlugin)
public:
    explicit DummyBackendPlugin(QObject *parent = nullptr);

    QString pluginId() const override;
    QString displayName() const override;
    QIcon   icon() const override;
    QString description() const override;
    QString version() const override;

    QStringList claimedDatabases() const override;
    ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *host,
                                     PalmDeviceConnection         *device) override;
};

#endif // WILDPALMS_DUMMYBACKENDPLUGIN_H
