#ifndef WILDPALMS_INSTALL_INSTALLACTIONPLUGIN_H
#define WILDPALMS_INSTALL_INSTALLACTIONPLUGIN_H

#include <QObject>

#include "core/ipluginaction.h"

namespace WildPalms {

class InstallActionPlugin : public QObject, public IPluginAction
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IPluginAction)

public:
    explicit InstallActionPlugin(QObject *parent = nullptr);

    // ===== IPlugin =====
    QString pluginId()    const override { return QStringLiteral("install"); }
    QString displayName() const override { return QStringLiteral("Install Files"); }
    QString description() const override
    { return QStringLiteral("Install .prc / .pdb files onto the connected Palm"); }
    QString version()     const override { return QStringLiteral("2.0.0"); }
    QIcon   icon()        const override;

    bool hasSettings() const override { return false; }

    // ===== IPluginAction =====
    bool execute(ActionContext       *ctx,
                  PalmDeviceConnection *device,
                  const QJsonObject   &parameters) override;

    Preconditions preconditions() const override
    {
        return { /*requiresDeviceConnection=*/ true,
                  /*requiresFiles=*/             {} };
    }

Q_SIGNALS:
    void fileInstalled(const QString &path);
    void fileFailed(const QString &path, const QString &errorMessage);
};

} // namespace WildPalms

#endif // WILDPALMS_INSTALL_INSTALLACTIONPLUGIN_H
