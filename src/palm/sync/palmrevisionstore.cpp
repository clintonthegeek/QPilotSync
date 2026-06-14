#include "palmrevisionstore.h"
#include <QSettings>

namespace WildPalms::PalmSync {

PalmRevisionStore::PalmRevisionStore(const QString &filePath)
    : m_filePath(filePath) {}

QString PalmRevisionStore::token(const QString &collectionId) const
{
    QSettings s(m_filePath, QSettings::IniFormat);
    return s.value(QStringLiteral("revisions/") + collectionId).toString();
}

void PalmRevisionStore::setToken(const QString &collectionId, const QString &token)
{
    QSettings s(m_filePath, QSettings::IniFormat);
    s.setValue(QStringLiteral("revisions/") + collectionId, token);
}

} // namespace WildPalms::PalmSync
