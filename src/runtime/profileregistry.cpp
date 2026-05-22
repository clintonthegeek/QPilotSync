#include "profileregistry.h"

#include <QDir>
#include <QRegularExpression>

namespace WildPalms::Runtime {

ProfileRegistry::ProfileRegistry(QObject *parent)
    : QObject(parent)
    , m_defaultRoot(QDir::homePath() + QStringLiteral("/.wildpalms"))
    , m_config(KSharedConfig::openConfig())
{
    load();
}

ProfileRegistry::ProfileRegistry(KSharedConfig::Ptr config, QObject *parent)
    : QObject(parent)
    , m_defaultRoot(QDir::homePath() + QStringLiteral("/.wildpalms"))
    , m_config(config)
{
    load();
}

ProfileRegistry::~ProfileRegistry() = default;

QList<ProfileEntry> ProfileRegistry::entries() const
{
    return m_cache;
}

ProfileEntry ProfileRegistry::entry(const QString &id) const
{
    for (const auto &e : m_cache)
        if (e.id == id) return e;
    return ProfileEntry{};
}

QString ProfileRegistry::lastActiveId() const
{
    return m_lastActiveId;
}

QString ProfileRegistry::defaultRoot() const
{
    return m_defaultRoot;
}

void ProfileRegistry::setDefaultRoot(const QString &root)
{
    m_defaultRoot = root;
}

QString ProfileRegistry::allocateNewId() const
{
    for (int i = 1; ; ++i) {
        const QString id = QStringLiteral("profile%1").arg(i);
        bool taken = false;
        for (const auto &e : m_cache) {
            if (e.id == id) { taken = true; break; }
        }
        if (!taken) return id;
    }
}

bool ProfileRegistry::isValidIdChars(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_-]+$"));
    return re.match(id).hasMatch();
}

ProfileEntry ProfileRegistry::registerNew(const QString & /*name*/,
                                          const QString & /*customPath*/)
{
    // Implemented in Task 5.
    return ProfileEntry{};
}

ProfileEntry ProfileRegistry::registerExisting(const QString & /*path*/)
{
    // Implemented in Task 6.
    return ProfileEntry{};
}

bool ProfileRegistry::unregister(const QString & /*id*/)
{
    // Implemented in Task 7.
    return false;
}

void ProfileRegistry::setLastActive(const QString & /*id*/)
{
    // Implemented in Task 7.
}

void ProfileRegistry::load()
{
    // Implemented in Task 4.
}

void ProfileRegistry::save() const
{
    // Implemented in Task 4.
}

} // namespace WildPalms::Runtime
