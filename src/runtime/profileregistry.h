#ifndef WILDPALMS_RUNTIME_PROFILEREGISTRY_H
#define WILDPALMS_RUNTIME_PROFILEREGISTRY_H

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include <KSharedConfig>

namespace WildPalms::Runtime {

/// A single entry in the app-level profile registry.
///
/// `id` is sticky: it matches the on-disk directory basename of the
/// profile and never changes for the life of the profile (even if
/// the user renames the display name). New profiles get the next
/// free `profileN` integer suffix.
///
/// `lastOpened` drives sorting in entries() and last-active recovery.
struct ProfileEntry {
    QString   id;
    QString   name;
    QString   path;
    QDateTime lastOpened;
    QString   usbSerial;

    bool isValid() const { return !id.isEmpty(); }
};

/// App-level profile registry persisted at
/// ~/.config/wildpalms/wildpalmsrc. One [profile-<id>] group per
/// registered profile plus [General]/lastActiveProfileId.
///
/// One instance per running app; KF6MainWindow owns it. Tests get a
/// second constructor that accepts an explicit KSharedConfig::Ptr +
/// override the default profile root via setDefaultRoot().
class ProfileRegistry : public QObject {
    Q_OBJECT
public:
    explicit ProfileRegistry(QObject *parent = nullptr);

    /// Test seam: use an explicit KSharedConfig (typically pointed at
    /// a QTemporaryDir) instead of the default per-user one. F.1a §11
    /// open implementation point — picked option (a).
    ProfileRegistry(KSharedConfig::Ptr config, QObject *parent = nullptr);

    ~ProfileRegistry() override;

    QList<ProfileEntry> entries() const;
    ProfileEntry        entry(const QString &id) const;
    QString             lastActiveId() const;

    ProfileEntry registerNew(const QString &name,
                             const QString &customPath = QString());
    ProfileEntry registerExisting(const QString &path);
    bool         unregister(const QString &id);
    void         setLastActive(const QString &id);

    /// Rename a registered profile. Updates both the registry
    /// (wildpalmsrc) and the per-profile profile.conf's [profile]/name.
    /// Trims newName; returns false if id is unknown, newName is empty
    /// after trimming, or the profile.conf write fails (in-memory
    /// cache is rolled back on disk failure). Emits entryUpdated(id)
    /// on success.
    bool rename(const QString &id, const QString &newName);

    ProfileEntry findBySerial(const QString &usbSerial) const;
    bool         bindSerial(const QString &id, const QString &usbSerial);

    QString defaultRoot() const;
    void    setDefaultRoot(const QString &root);

    QString allocateNewId() const;

signals:
    void registryChanged();
    void entryUpdated(QString id);

private:
    QString             m_defaultRoot;
    KSharedConfig::Ptr  m_config;
    QList<ProfileEntry> m_cache;
    QString             m_lastActiveId;

    void load();
    void save() const;

    static bool isValidIdChars(const QString &id);
};

} // namespace WildPalms::Runtime

#endif // WILDPALMS_RUNTIME_PROFILEREGISTRY_H
