#include "accountcontroller.h"

#include "palmruntime.h"
#include "../profile.h"

// libkalburator includes — bare names (libkalburator headers are on the
// include path; matches the existing palmruntime.cpp include style).
#include "providermanager.h"
#include "iprovider.h"
#include "backendregistry.h"
#include "backendconfiguration.h"
#include "collectioninfo.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>

namespace WildPalms::Runtime {

using Kalburator::Sync::ProviderManager;
using Kalburator::Sync::IProvider;
using Kalburator::Sync::BackendConfiguration;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::BackendRegistry;

AccountController::AccountController(const QString &syncFolderPath,
                                     BackendRegistry *registry,
                                     Profile *profile,
                                     PalmRuntime *palmRuntime,
                                     QObject *parent)
    : QObject(parent)
    , m_syncFolderPath(syncFolderPath)
    , m_registry(registry)
    , m_profile(profile)
    , m_palmRuntime(palmRuntime)
    , m_providerManager(std::make_unique<ProviderManager>(registry, this))
{
    Q_ASSERT(m_registry);
    Q_ASSERT(m_profile);
    Q_ASSERT(m_palmRuntime);

    connect(m_providerManager.get(), &ProviderManager::providersChanged,
            this, &AccountController::providersChanged);
    connect(m_providerManager.get(),
            &ProviderManager::providerConnectionStateChanged,
            this, [this](const QString &providerId, bool connected) {
        const ConnectionState s = connected
            ? ConnectionState::Connected
            : ConnectionState::Disconnected;
        m_states.insert(providerId, s);
        emit connectStateChanged(providerId, s);
    });

    loadAndConnect();
}

AccountController::~AccountController() = default;

QString AccountController::sidecarPath() const {
    return QDir(m_syncFolderPath).filePath(QStringLiteral(".wildpalms.providers"));
}

void AccountController::loadAndConnect() {
    KConfig cfg(sidecarPath(), KConfig::SimpleConfig);
    KConfigGroup root = cfg.group(QStringLiteral("Providers"));
    m_providerManager->loadFromProfile(root);
    for (IProvider *p : m_providerManager->providers()) {
        m_states.insert(p->id(), ConnectionState::Connecting);
    }
    m_providerManager->connectAll();
}

void AccountController::persist() {
    KConfig cfg(sidecarPath(), KConfig::SimpleConfig);
    KConfigGroup root = cfg.group(QStringLiteral("Providers"));
    m_providerManager->saveToProfile(root);
    cfg.sync();
}

// Stubs — filled in Tasks 5/6.
QString AccountController::addProvider(const QString &/*kind*/,
                                       const BackendConfiguration &/*config*/) {
    return QString();
}

bool AccountController::removeProvider(const QString &/*providerId*/) {
    return false;
}

QList<IProvider*> AccountController::providers() const {
    return m_providerManager->providers();
}

QList<CollectionInfo> AccountController::collectionsFor(const QString &id) const {
    if (auto *p = m_providerManager->providerById(id)) return p->collections();
    return {};
}

AccountController::ConnectionState
AccountController::stateFor(const QString &id) const {
    return m_states.value(id, ConnectionState::Disconnected);
}

QString AccountController::errorFor(const QString &id) const {
    return m_lastErrors.value(id);
}

int AccountController::mappingCountFor(const QString &id) const {
    return mappingIndicesFor(id).size();
}

QStringList AccountController::mappingDescriptionsFor(const QString &/*id*/,
                                                     int /*max*/) const {
    // Filled in Task 6.
    return {};
}

ProviderManager *AccountController::providerManager() const {
    return m_providerManager.get();
}

QList<int> AccountController::mappingIndicesFor(const QString &id) const {
    QList<int> out;
    const QJsonArray arr = m_profile->syncMappingsJson();
    const QString prefix = id + QStringLiteral(":");
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject row = arr.at(i).toObject();
        const QString src = row.value(QStringLiteral("sourceBackend")).toString();
        const QString tgt = row.value(QStringLiteral("targetBackend")).toString();
        if (src.startsWith(prefix) || tgt.startsWith(prefix)) out.append(i);
    }
    return out;
}

}  // namespace WildPalms::Runtime
