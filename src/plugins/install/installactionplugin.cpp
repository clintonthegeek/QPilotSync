#include "installactionplugin.h"

#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>

#include <KPluginFactory>

#include "palm/palmdeviceconnection.h"
#include "palm/device/ipalmfileinstaller.h"

namespace WildPalms {

InstallActionPlugin::InstallActionPlugin(QObject *parent)
    : QObject(parent)
{
}

QIcon InstallActionPlugin::icon() const
{
    return QIcon::fromTheme(QStringLiteral("document-import"));
}

bool InstallActionPlugin::execute(ActionContext       *ctx,
                                    PalmDeviceConnection *device,
                                    const QJsonObject   &parameters)
{
    auto *installer = device ? device->fileInstaller() : nullptr;
    if (!installer) {
        if (ctx) ctx->log(QStringLiteral("Install: no file installer available"));
        return false;
    }

    const QJsonArray files = parameters.value(QStringLiteral("files")).toArray();
    if (ctx) ctx->setTotal(files.size());

    int succeeded = 0;
    int failed    = 0;
    bool cancelled = false;

    for (int i = 0; i < files.size(); ++i) {
        if (ctx && ctx->isCancelled()) {
            cancelled = true;
            if (ctx) ctx->log(QStringLiteral("Install: cancelled at %1/%2")
                               .arg(i).arg(files.size()));
            break;
        }
        const QJsonObject f = files[i].toObject();
        const QString path  = f.value(QStringLiteral("path")).toString();
        const QString name  = f.value(QStringLiteral("display_name")).toString();

        QString err;
        const bool ok = installer->installFile(path, &err);
        if (ok) {
            ++succeeded;
            if (ctx) ctx->log(QStringLiteral("Installed: %1").arg(name));
            Q_EMIT fileInstalled(path);
        } else {
            ++failed;
            if (ctx) ctx->log(QStringLiteral("Failed: %1 (%2)").arg(name, err));
            Q_EMIT fileFailed(path, err);
        }
        if (ctx) ctx->setCurrent(i + 1);
    }

    if (ctx) ctx->log(QStringLiteral("Install: %1 succeeded, %2 failed%3")
                       .arg(succeeded)
                       .arg(failed)
                       .arg(cancelled ? QStringLiteral(" (cancelled)") : QString()));
    return failed == 0 && !cancelled;
}

} // namespace WildPalms

K_PLUGIN_FACTORY_WITH_JSON(
    InstallActionPluginFactory,
    "install-action-plugin.json",
    registerPlugin<WildPalms::InstallActionPlugin>();)

#include "installactionplugin.moc"
