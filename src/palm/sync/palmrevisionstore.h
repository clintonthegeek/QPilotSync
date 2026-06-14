#ifndef WILDPALMS_PALMSYNC_PALMREVISIONSTORE_H
#define WILDPALMS_PALMSYNC_PALMREVISIONSTORE_H

#include <QString>

namespace WildPalms::PalmSync {

/// Persists per-collection revision tokens (Palm DB modnums) across runs.
/// QSettings(ini)-backed; analogue of the lib's AkonadiRevisionStore.
class PalmRevisionStore {
public:
    explicit PalmRevisionStore(const QString &filePath);
    QString token(const QString &collectionId) const;
    void    setToken(const QString &collectionId, const QString &token);
private:
    QString m_filePath;
};

} // namespace WildPalms::PalmSync
#endif
