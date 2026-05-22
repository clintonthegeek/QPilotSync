#ifndef WILDPALMS_KF6_PROFILEMENUCONTROLLER_H
#define WILDPALMS_KF6_PROFILEMENUCONTROLLER_H

#include <QObject>
#include <QString>

namespace WildPalms::Runtime { class ProfileRegistry; }
class KActionCollection;
class KActionMenu;

/// Owns the Switch ▸ and Forget ▸ KActionMenu instances under
/// File → Profile, and keeps their contents synced with
/// ProfileRegistry. F.1b §5 / spec
/// docs/superpowers/specs/2026-05-22-f1b-file-menu-design.md
class ProfileMenuController : public QObject {
    Q_OBJECT
public:
    ProfileMenuController(WildPalms::Runtime::ProfileRegistry *registry,
                          KActionCollection *actionCollection,
                          QObject *parent = nullptr);
    ~ProfileMenuController() override;

    KActionMenu *switchMenu() const { return m_switchMenu; }
    KActionMenu *forgetMenu() const { return m_forgetMenu; }

    /// Marks one entry as the currently-loaded profile. Causes its
    /// row in Switch ▸ to gain a checkmark + be disabled, and its
    /// row in Forget ▸ to be disabled. Pass an empty string when no
    /// profile is loaded.
    void setActiveProfileId(const QString &id);

signals:
    void switchRequested(QString id);
    void forgetRequested(QString id);

private:
    void rebuild();

    WildPalms::Runtime::ProfileRegistry *m_registry;
    KActionCollection *m_actionCollection;
    KActionMenu       *m_switchMenu = nullptr;
    KActionMenu       *m_forgetMenu = nullptr;
    QString            m_activeId;
};

#endif // WILDPALMS_KF6_PROFILEMENUCONTROLLER_H
