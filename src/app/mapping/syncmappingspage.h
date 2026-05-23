#ifndef WILDPALMS_APP_MAPPING_SYNCMAPPINGSPAGE_H
#define WILDPALMS_APP_MAPPING_SYNCMAPPINGSPAGE_H

#include <QWidget>
#include <QJsonArray>
#include <QPointer>

class Profile;
class QLabel;

namespace WildPalms::Runtime {
    class AccountController;
    class PalmRuntime;
}

namespace WildPalms::AppMapping {

class SyncMappingGraphView;
class MappingInspectorPanel;

class SyncMappingsPage : public QWidget {
    Q_OBJECT
public:
    SyncMappingsPage(Profile *profile,
                     WildPalms::Runtime::AccountController *accounts,
                     WildPalms::Runtime::PalmRuntime *palmRuntime,
                     QWidget *parent = nullptr);

    /// Persist current mappings into the supplied Profile (typically the
    /// same one passed at construction). Called by SettingsDialog on OK
    /// / Apply.
    void applyTo(Profile *profile);

    /// Current in-memory mappings (the live graph state).
    QJsonArray currentMappings() const;

private slots:
    void onSyncStarted();
    void onSyncFinished();
    void onMappingEdited(const QString &mappingId, const QJsonObject &updatedJson);
    void onEdgeSelected(const QString &mappingId);

private:
    void buildUi();
    void reloadGraph();
    void setReadOnlyBannerVisible(bool visible);

    // Profile is not a QObject — borrow as raw pointer. Lifetime is
    // guaranteed by the caller (SettingsDialog / MainWindow own it).
    Profile                                        *m_profile {nullptr};
    QPointer<WildPalms::Runtime::AccountController> m_accounts;
    QPointer<WildPalms::Runtime::PalmRuntime>       m_palmRuntime;

    SyncMappingGraphView  *m_graphView {nullptr};
    MappingInspectorPanel *m_inspector {nullptr};
    QLabel                *m_readOnlyBanner {nullptr};
};

} // namespace WildPalms::AppMapping

#endif
