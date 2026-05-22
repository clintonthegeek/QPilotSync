#include "profileregistry.h"

#include <KConfigGroup>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

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
    int high = 0;
    static const QRegularExpression re(QStringLiteral("^profile(\\d+)$"));
    auto scan = [&](const QString &candidate) {
        const auto m = re.match(candidate);
        if (m.hasMatch())
            high = std::max(high, m.captured(1).toInt());
    };

    for (const auto &e : m_cache) scan(e.id);

    const QStringList groups = m_config->groupList();
    for (const QString &g : groups) {
        if (g.startsWith(QStringLiteral("profile-")))
            scan(g.mid(QStringLiteral("profile-").size()));
        else if (g.startsWith(QStringLiteral("tombstone-")))
            scan(g.mid(QStringLiteral("tombstone-").size()));
    }
    return QStringLiteral("profile%1").arg(high + 1);
}

bool ProfileRegistry::isValidIdChars(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_-]+$"));
    return re.match(id).hasMatch();
}

ProfileEntry ProfileRegistry::registerNew(const QString &name,
                                          const QString &customPath)
{
    ProfileEntry e;
    e.name       = name;
    e.lastOpened = QDateTime::currentDateTimeUtc();

    if (customPath.isEmpty()) {
        e.id   = allocateNewId();
        e.path = m_defaultRoot + QLatin1Char('/') + e.id;
    } else {
        const QString basename = QFileInfo(customPath).fileName();
        if (!isValidIdChars(basename))
            return ProfileEntry{};
        // Reject if id is already registered.
        for (const auto &existing : m_cache) {
            if (existing.id == basename)
                return ProfileEntry{};
        }
        e.id   = basename;
        e.path = customPath;
    }

    // Create the directory.
    QDir dir(e.path);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return ProfileEntry{};

    // Write stub profile.conf so the profile is immediately loadable
    // by Profile::load(). Spec §11 open implementation point — picked
    // option (a).
    {
        const QString confPath = e.path + QStringLiteral("/profile.conf");
        QSettings s(confPath, QSettings::IniFormat);
        s.setValue(QStringLiteral("meta/schemaVersion"), 1);
        s.setValue(QStringLiteral("profile/id"), e.id);
        s.setValue(QStringLiteral("profile/name"), e.name);
        s.sync();
        if (s.status() != QSettings::NoError)
            return ProfileEntry{};
    }

    m_cache.append(e);
    // Re-sort so newest is first.
    std::sort(m_cache.begin(), m_cache.end(),
              [](const ProfileEntry &a, const ProfileEntry &b) {
                  return a.lastOpened > b.lastOpened;
              });

    save();
    emit registryChanged();
    return e;
}

ProfileEntry ProfileRegistry::registerExisting(const QString &path)
{
    const QString confPath = path + QStringLiteral("/profile.conf");
    if (!QFile::exists(confPath))
        return ProfileEntry{};

    QSettings s(confPath, QSettings::IniFormat);
    const QString id   = s.value(QStringLiteral("profile/id")).toString();
    const QString name = s.value(QStringLiteral("profile/name")).toString();
    if (id.isEmpty())
        return ProfileEntry{};

    // id must match directory basename.
    const QString basename = QFileInfo(path).fileName();
    if (id != basename)
        return ProfileEntry{};

    // Reject if id already in cache (possibly at a different path).
    for (const auto &existing : m_cache) {
        if (existing.id == id)
            return ProfileEntry{};
    }

    ProfileEntry e;
    e.id         = id;
    e.name       = name;
    e.path       = path;
    e.lastOpened = QDateTime::currentDateTimeUtc();

    m_cache.append(e);
    std::sort(m_cache.begin(), m_cache.end(),
              [](const ProfileEntry &a, const ProfileEntry &b) {
                  return a.lastOpened > b.lastOpened;
              });

    save();
    emit registryChanged();
    return e;
}

bool ProfileRegistry::unregister(const QString &id)
{
    bool found = false;
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        if (it->id == id) {
            m_cache.erase(it);
            found = true;
            break;
        }
    }
    if (!found) return false;

    if (m_lastActiveId == id)
        m_lastActiveId.clear();

    // Write a tombstone group so allocateNewId() still sees the id was used.
    KConfigGroup tomb(m_config, QStringLiteral("tombstone-") + id);
    tomb.writeEntry("retired", true);

    save();
    emit registryChanged();
    return true;
}

void ProfileRegistry::setLastActive(const QString &id)
{
    bool found = false;
    for (auto &e : m_cache) {
        if (e.id == id) {
            e.lastOpened = QDateTime::currentDateTimeUtc();
            found = true;
            break;
        }
    }
    if (!found) return;

    m_lastActiveId = id;
    std::sort(m_cache.begin(), m_cache.end(),
              [](const ProfileEntry &a, const ProfileEntry &b) {
                  return a.lastOpened > b.lastOpened;
              });
    save();
    emit entryUpdated(id);
}

bool ProfileRegistry::rename(const QString &id, const QString &newName)
{
    const QString trimmed = newName.trimmed();
    if (id.isEmpty() || trimmed.isEmpty()) return false;

    int idx = -1;
    for (int i = 0; i < m_cache.size(); ++i) {
        if (m_cache[i].id == id) { idx = i; break; }
    }
    if (idx < 0) return false;

    const QString oldName = m_cache[idx].name;
    m_cache[idx].name = trimmed;

    const QString confPath =
        m_cache[idx].path + QStringLiteral("/profile.conf");
    {
        QSettings s(confPath, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("profile"));
        s.setValue(QStringLiteral("name"), trimmed);
        s.endGroup();
        s.sync();
        if (s.status() != QSettings::NoError) {
            m_cache[idx].name = oldName;
            return false;
        }
    }

    save();
    emit entryUpdated(id);
    return true;
}

void ProfileRegistry::load()
{
    m_cache.clear();
    m_lastActiveId.clear();

    const QStringList groups = m_config->groupList();
    for (const QString &g : groups) {
        if (!g.startsWith(QStringLiteral("profile-"))) continue;
        const QString id = g.mid(QStringLiteral("profile-").size());
        if (id.isEmpty()) continue;

        KConfigGroup cg(m_config, g);
        ProfileEntry e;
        e.id         = id;
        e.name       = cg.readEntry("name", QString());
        e.path       = cg.readEntry("path", QString());
        e.lastOpened = cg.readEntry("lastOpened", QDateTime());
        m_cache.append(e);
    }

    KConfigGroup gen(m_config, QStringLiteral("General"));
    m_lastActiveId = gen.readEntry("lastActiveProfileId", QString());

    // Sort by lastOpened descending (most recent first).
    std::sort(m_cache.begin(), m_cache.end(),
              [](const ProfileEntry &a, const ProfileEntry &b) {
                  return a.lastOpened > b.lastOpened;
              });
}

void ProfileRegistry::save() const
{
    // Clear all profile-* groups first.
    const QStringList groups = m_config->groupList();
    for (const QString &g : groups) {
        if (g.startsWith(QStringLiteral("profile-")))
            m_config->deleteGroup(g);
    }

    // Re-write each cache entry.
    for (const auto &e : m_cache) {
        KConfigGroup cg(m_config, QStringLiteral("profile-") + e.id);
        cg.writeEntry("name", e.name);
        cg.writeEntry("path", e.path);
        cg.writeEntry("lastOpened", e.lastOpened);
    }

    // [General]/lastActiveProfileId.
    KConfigGroup gen(m_config, QStringLiteral("General"));
    gen.writeEntry("lastActiveProfileId", m_lastActiveId);

    m_config->sync();
}

} // namespace WildPalms::Runtime
