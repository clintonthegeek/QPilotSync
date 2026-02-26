#ifndef INSTALLCONDUIT_PLUGIN_H
#define INSTALLCONDUIT_PLUGIN_H

#include <QObject>
#include <QIcon>
#include "core/iconduit.h"

class InstallView;

namespace Sync {
class SyncContext;
struct SyncResult;
}

class InstallConduit : public QObject, public IConduit
{
    Q_OBJECT
    Q_INTERFACES(IConduit)
    Q_PROPERTY(QString installFolder READ installFolder WRITE setInstallFolder)

public:
    explicit InstallConduit(QObject *parent = nullptr);
    ~InstallConduit() override = default;

    // IConduit Identity
    QString conduitId() const override { return QStringLiteral("install"); }
    QString displayName() const override { return QStringLiteral("Install Files"); }
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
    QString viewName() const override { return QStringLiteral("Install"); }
    QIcon viewIcon() const override;

    // Install-specific
    void setInstallFolder(const QString &path);
    QString installFolder() const { return m_installFolder; }
    QStringList pendingFiles(const QString &installFolder) const;
    QStringList installedFiles(const QString &installFolder) const;

private:
    bool installFile(const QString &filePath, int socket);
    bool moveToInstalled(const QString &filePath, const QString &installFolder);
    void ensureFoldersExist(const QString &installFolder);

    QString m_installFolder;
    InstallView *m_view = nullptr;
};

#endif // INSTALLCONDUIT_PLUGIN_H
