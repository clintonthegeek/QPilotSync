#ifndef WILDPALMS_APP_WIZARD_DISCOVERYROW_H
#define WILDPALMS_APP_WIZARD_DISCOVERYROW_H

#include <QWidget>
#include <QFutureWatcher>
#include <memory>

class QLabel;
class QListWidget;
class QPushButton;

namespace Kalburator::Sync {
    class BackendRegistry;
    class IProvider;
    struct BackendConfiguration;
}

namespace WildPalms::Wizard {

struct WizardAccount;

/// One row on the DiscoveryPage. Builds an IProvider from the
/// account's BackendConfiguration, calls connect(), surfaces the
/// resulting collections in a single-select list. Required to reach
/// "Chosen" state before the page can be considered complete.
class DiscoveryRow : public QWidget {
    Q_OBJECT
public:
    enum State { Loading, Loaded, Empty, Failed, Chosen };

    DiscoveryRow(Kalburator::Sync::BackendRegistry *registry,
                 const WizardAccount &account,
                 QWidget *parent = nullptr);
    ~DiscoveryRow() override;

    State state() const { return m_state; }
    QString chosenCollectionId() const { return m_chosenCollectionId; }

    void runDiscovery();

signals:
    void readinessChanged();

private:
    void setState(State s);
    void onConnectFinished();
    void onSelectionChanged();

    Kalburator::Sync::BackendRegistry *m_registry;
    std::unique_ptr<Kalburator::Sync::IProvider> m_provider;
    QString m_displayName;
    State   m_state {Loading};

    QLabel       *m_status   {nullptr};
    QListWidget  *m_list     {nullptr};
    QPushButton  *m_retry    {nullptr};
    QString       m_chosenCollectionId;

    QFutureWatcher<bool> m_watcher;
};

}  // namespace WildPalms::Wizard

#endif
