#ifndef WILDPALMS_DUMMYACTIONPLUGIN_H
#define WILDPALMS_DUMMYACTIONPLUGIN_H

#include "core/ipluginaction.h"

#include <QObject>

class DummyActionPlugin : public QObject, public WildPalms::IPluginAction
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IPluginAction)
public:
    explicit DummyActionPlugin(QObject *parent = nullptr);

    QString pluginId() const override;
    QString displayName() const override;
    QIcon   icon() const override;
    QString description() const override;
    QString version() const override;

    bool execute(ActionContext       *ctx,
                 PalmDeviceConnection *device,
                 const QJsonObject   &parameters) override;
    Preconditions preconditions() const override;
};

#endif // WILDPALMS_DUMMYACTIONPLUGIN_H
