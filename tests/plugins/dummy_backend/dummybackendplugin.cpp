#include "dummybackendplugin.h"

#include <iblobbackend.h>
#include <KPluginFactory>

#include <QIcon>

DummyBackendPlugin::DummyBackendPlugin(QObject *parent) : QObject(parent) {}

QString DummyBackendPlugin::pluginId() const    { return QStringLiteral("dummy_backend"); }
QString DummyBackendPlugin::displayName() const { return QStringLiteral("Dummy Backend Plugin"); }
QIcon   DummyBackendPlugin::icon() const        { return QIcon::fromTheme(QStringLiteral("application-x-executable")); }
QString DummyBackendPlugin::description() const { return QStringLiteral("E.8 test fixture."); }
QString DummyBackendPlugin::version() const     { return QStringLiteral("1.0"); }

QStringList DummyBackendPlugin::claimedDatabases() const
{
    return {QStringLiteral("DummyDB")};
}

std::unique_ptr<Kalburator::Sync::IBlobBackend>
DummyBackendPlugin::createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *)
{
    return nullptr; // no real backends; the factory-path test only needs the object to exist.
}

K_PLUGIN_FACTORY_WITH_JSON(DummyBackendPluginFactory,
                           "dummy-backend-plugin.json",
                           registerPlugin<DummyBackendPlugin>();)

#include "dummybackendplugin.moc"
