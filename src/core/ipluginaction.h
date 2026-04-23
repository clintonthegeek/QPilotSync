#ifndef WILDPALMS_IPLUGINACTION_H
#define WILDPALMS_IPLUGINACTION_H

#include "iplugin.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class PalmDeviceConnection;

namespace WildPalms {

/**
 * @brief One-shot triggerable plugin kind.
 *
 * Actions do not participate in record-level sync. Used for
 * Install-file, Backup/Restore, Format-card, etc. Execution is
 * synchronous from the caller's perspective; callers run it on a
 * worker thread. Progress and log output surface through a QObject
 * proxy (ActionContext) rather than the IPluginAction itself, so
 * actions remain cheap to load (no QObject overhead per plugin).
 */
class IPluginAction : public IPlugin
{
public:
    class ActionContext : public QObject
    {
        Q_OBJECT
    public:
        using QObject::QObject;
        ~ActionContext() override = default;

        virtual void setTotal(int total)      = 0;
        virtual void setCurrent(int current)  = 0;
        virtual void log(const QString &msg)  = 0;
        virtual bool isCancelled() const      = 0;

    Q_SIGNALS:
        void progress(int current, int total);
        void message(const QString &msg);
    };

    /// Runs the action. Returns true on success. Called on a worker
    /// thread by PluginActionManager::runAction(); actions MUST NOT
    /// assume a GUI thread.
    virtual bool execute(ActionContext       *ctx,
                         PalmDeviceConnection *device,
                         const QJsonObject   &parameters) = 0;

    /// What must be true before the manager offers this action.
    struct Preconditions {
        bool        requiresDeviceConnection = true;
        QStringList requiresFiles;
    };
    virtual Preconditions preconditions() const = 0;
};

} // namespace WildPalms

Q_DECLARE_INTERFACE(WildPalms::IPluginAction,
                    "ca.vibekoder.WildPalms.IPluginAction/1.0")

#endif // WILDPALMS_IPLUGINACTION_H
