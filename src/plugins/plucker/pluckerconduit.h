#ifndef PLUCKERCONDUIT_H
#define PLUCKERCONDUIT_H

#include <QObject>
#include <QIcon>
#include "core/itoolconduit.h"
#include "pluckerconfig.h"

namespace Sync {
class SyncContext;
struct SyncResult;
}

class PluckerConduit : public QObject, public IToolConduit
{
    Q_OBJECT
    Q_INTERFACES(IConduit IToolConduit)

public:
    explicit PluckerConduit(QObject *parent = nullptr);
    ~PluckerConduit() override = default;

    // IConduit Identity
    QString conduitId() const override { return QStringLiteral("plucker"); }
    QString displayName() const override { return QStringLiteral("Plucker"); }
    QIcon icon() const override;
    QString description() const override;
    QString version() const override { return QStringLiteral("1.0.0"); }

    // IConduit Capabilities
    bool requiresDevice() const override { return true; }

    // IConduit Sync
    Sync::SyncResult sync(Sync::SyncContext *context) override;
    bool canSync(const Sync::SyncContext *context) const override;
    bool shouldRun(const Sync::SyncContext *context) const override;

    // IConduit UI
    bool hasView() const override { return true; }
    QWidget *createView(QWidget *parent) override;
    QString viewName() const override { return QStringLiteral("Plucker"); }
    QIcon viewIcon() const override;

    // IToolConduit
    QString toolPath() const override;
    bool prepareExecution(Sync::SyncContext *context) override;
    bool installResults(Sync::SyncContext *context) override;

Q_SIGNALS:
    void logMessage(const QString &message);
    void errorOccurred(const QString &error);
    void progressUpdated(int current, int total, const QString &message);

private:
    bool spiderChannel(const PluckerChannel &channel, const QString &outputDir);
    QString findPython() const;
    QString parserPath() const;
    QString viewerPath() const;

    PluckerConfig m_config;
    QString m_outputDir;
    QStringList m_producedFiles;
};

#endif // PLUCKERCONDUIT_H
