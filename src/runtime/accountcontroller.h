#ifndef WILDPALMS_RUNTIME_ACCOUNTCONTROLLER_H
#define WILDPALMS_RUNTIME_ACCOUNTCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QFuture>
#include <QJsonArray>
#include <memory>

class Profile;
namespace Kalburator::Sync {
    class ProviderManager;
    class BackendRegistry;
    class IProvider;
    struct BackendConfiguration;
    struct CollectionInfo;
}
namespace WildPalms::Runtime {
    class PalmRuntime;
}

namespace WildPalms::Runtime {

/// Profile-scoped owner of provider lifecycle and provider-bound mapping
/// integrity. Constructed in KF6MainWindow::loadProfile() AFTER PalmRuntime;
/// torn down BEFORE PalmRuntime in closeProfile() / loadProfile().
///
/// AccountController does NOT bind anything to anything — bindings (which
/// Palm slot ↔ which backend) live in SyncMappings, edited via MappingEditor.
/// AccountController only manages credentials, connection state, and cascades
/// mapping deletion when an account is removed.
///
/// Persistence: <syncFolderPath>/.wildpalms.providers (KConfig sidecar to
/// .wildpalms.conf). Same shape PlanStan adopted in Phase H.5.
class AccountController : public QObject {
    Q_OBJECT
public:
    enum class ConnectionState {
        Disconnected,
        Connecting,
        Connected,
        Error,
    };
    Q_ENUM(ConnectionState)

    AccountController(const QString &syncFolderPath,
                      Kalburator::Sync::BackendRegistry *registry,
                      Profile *profile,
                      PalmRuntime *palmRuntime,
                      QObject *parent = nullptr);
    ~AccountController() override;

    /// Add a new provider. Persists immediately; connect() runs async.
    /// Refuses if palmRuntime->isRunning(); returns empty string on refusal.
    /// (Stubbed in Task 3; full impl in Task 5.)
    QString addProvider(const QString &kind,
                        const Kalburator::Sync::BackendConfiguration &config);

    /// Remove a provider AND cascade-delete mappings referencing its backends.
    /// Refuses if palmRuntime->isRunning(); returns false on refusal.
    /// (Stubbed in Task 3; full impl in Tasks 5–6.)
    bool removeProvider(const QString &providerId);

    QList<Kalburator::Sync::IProvider*> providers() const;
    QList<Kalburator::Sync::CollectionInfo>
        collectionsFor(const QString &providerId) const;
    ConnectionState stateFor(const QString &providerId) const;
    QString errorFor(const QString &providerId) const;
    int mappingCountFor(const QString &providerId) const;
    QStringList mappingDescriptionsFor(const QString &providerId, int max) const;

    /// Backing ProviderManager (used by AccountsPage to wire signals and by
    /// MappingPromptDialog to look up providerById).
    Kalburator::Sync::ProviderManager *providerManager() const;

    /// Append rows to Profile::syncMappingsJson and persist. Used by
    /// MappingPromptDialog to bind a freshly-added provider's collections
    /// to Palm slots. The caller decides slot semantics; AC just persists.
    void appendMappings(const QJsonArray &rows);

signals:
    void providersChanged();
    void connectStateChanged(QString providerId, ConnectionState state);
    void connectFailed(QString providerId, QString error);
    void mappingsChanged();   // emitted on cascade-delete (Task 6)

private:
    QString sidecarPath() const;
    void loadAndConnect();
    void persist();
    QList<int> mappingIndicesFor(const QString &providerId) const;

    QString                                          m_syncFolderPath;
    Kalburator::Sync::BackendRegistry               *m_registry;        // borrowed
    Profile                                         *m_profile;         // borrowed
    PalmRuntime                                     *m_palmRuntime;     // borrowed
    std::unique_ptr<Kalburator::Sync::ProviderManager> m_providerManager;
    QHash<QString, ConnectionState>                  m_states;
    QHash<QString, QString>                          m_lastErrors;
};

}  // namespace WildPalms::Runtime

#endif
