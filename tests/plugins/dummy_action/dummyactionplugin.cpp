#include "dummyactionplugin.h"

#include <KPluginFactory>

#include <QIcon>

DummyActionPlugin::DummyActionPlugin(QObject *parent) : QObject(parent) {}

QString DummyActionPlugin::pluginId() const    { return QStringLiteral("dummy_action"); }
QString DummyActionPlugin::displayName() const { return QStringLiteral("Dummy Action Plugin"); }
QIcon   DummyActionPlugin::icon() const        { return QIcon::fromTheme(QStringLiteral("system-run")); }
QString DummyActionPlugin::description() const { return QStringLiteral("E.8 test fixture."); }
QString DummyActionPlugin::version() const     { return QStringLiteral("1.0"); }

bool DummyActionPlugin::execute(ActionContext       *ctx,
                                PalmDeviceConnection *,
                                const QJsonObject   &)
{
    if (ctx) {
        ctx->setTotal(1);
        ctx->log(QStringLiteral("dummy_action running"));
        ctx->setCurrent(1);
    }
    return true;
}

WildPalms::IPluginAction::Preconditions DummyActionPlugin::preconditions() const
{
    Preconditions p;
    p.requiresDeviceConnection = false;
    return p;
}

K_PLUGIN_FACTORY_WITH_JSON(DummyActionPluginFactory,
                           "dummy-action-plugin.json",
                           registerPlugin<DummyActionPlugin>();)

#include "dummyactionplugin.moc"
