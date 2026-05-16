#include "accountcontroller.h"

#include "palmruntime.h"
#include "../profile.h"

// libkalburator includes — bare names (libkalburator headers are on the
// include path; matches the existing palmruntime.cpp include style).
#include "providermanager.h"
#include "iprovider.h"
#include "backendregistry.h"
#include "backendcontribution.h"
#include "backendconfiguration.h"
#include "collectioninfo.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>

#include <algorithm>

namespace WildPalms::Runtime {

using Kalburator::Sync::ProviderManager;
using Kalburator::Sync::IProvider;
using Kalburator::Sync::BackendConfiguration;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::BackendContribution;

AccountController::AccountController(const QString &syncFolderPath,
                                     BackendRegistry *registry,
                                     Profile *profile,
                                     PalmRuntime *palmRuntime,
                                     QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_profile(profile)
    , m_palmRuntime(palmRuntime)
    , m_providerManager(std::make_unique<ProviderManager>(registry, this))
{
    Q_UNUSED(syncFolderPath)
    Q_ASSERT(m_registry);
    Q_ASSERT(m_profile);
    Q_ASSERT(m_palmRuntime);

    QObject::connect(m_providerManager.get(), &ProviderManager::providersChanged,
            this, &AccountController::providersChanged);
    QObject::connect(m_providerManager.get(),
            &ProviderManager::providerConnectionStateChanged,
            this, [this](const QString &providerId, bool connected) {
        const ConnectionState s = connected
            ? ConnectionState::Connected
            : ConnectionState::Disconnected;
        m_states.insert(providerId, s);
        Q_EMIT connectStateChanged(providerId, s);
    });

    loadAndConnect();
}

AccountController::~AccountController() = default;

void AccountController::loadAndConnect() {
    for (const auto &cfg : m_profile->accounts()) {
        BackendContribution *contribution = m_registry->contributionFor(cfg.type);
        if (!contribution) continue;
        auto provider = contribution->createProvider(this);
        if (!provider) continue;
        provider->load(cfg);
        m_states.insert(cfg.id, ConnectionState::Connecting);
        m_providerManager->addProvider(std::move(provider));
    }
    m_providerManager->connectAll();

    // Re-apply disabled provider states so mappings start in the right state.
    for (const auto &cfg : m_profile->accounts()) {
        if (!cfg.enabled)
            setProviderEnabled(cfg.id, false);
    }
}

bool AccountController::providerEnabled(const QString &id) const {
    for (const auto &bc : m_profile->accounts()) {
        if (bc.id == id) return bc.enabled;
    }
    return true; // unknown → assume enabled
}

void AccountController::setProviderEnabled(const QString &providerId, bool enabled) {
    if (!m_profile) return;
    // Update the BackendConfiguration enabled flag
    auto accts = m_profile->accounts();
    auto it = std::find_if(accts.begin(), accts.end(),
        [&](const auto &bc){ return bc.id == providerId; });
    if (it == accts.end()) return;
    it->enabled = enabled;
    m_profile->saveAccount(*it);

    // Fan out to all mappings referencing this provider
    QJsonArray arr = m_profile->syncMappingsJson();
    const QString prefix = providerId + QLatin1Char(':');
    bool changed = false;
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject row = arr.at(i).toObject();
        const QString src = row.value(QStringLiteral("sourceBackend")).toString();
        const QString tgt = row.value(QStringLiteral("targetBackend")).toString();
        if (src.startsWith(prefix) || tgt.startsWith(prefix)) {
            row[QStringLiteral("enabled")] = enabled;
            arr.replace(i, row);
            changed = true;
        }
    }
    if (changed) {
        m_profile->setSyncMappingsJson(arr);
        m_profile->save();
    }
    Q_EMIT providerEnabledChanged(providerId, enabled);
}

QString AccountController::addProvider(const QString &kind,
                                       const BackendConfiguration &config) {
    if (m_palmRuntime->isRunning()) return QString();

    BackendContribution *contribution = m_registry->contributionFor(kind);
    if (!contribution) return QString();

    BackendConfiguration cfg = config;
    if (cfg.id.isEmpty()) {
        cfg.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    cfg.type = kind;

    auto provider = contribution->createProvider(this);
    if (!provider) return QString();
    provider->load(cfg);

    const QString id = cfg.id;
    m_profile->saveAccount(cfg);
    m_profile->save();

    m_providerManager->addProvider(std::move(provider));
    m_states.insert(id, ConnectionState::Connecting);

    // Kick off async connect for just-this-one (connectAll connects all,
    // including the just-added one — ProviderManager handles idempotence).
    m_providerManager->connectAll();

    return id;
}

bool AccountController::removeProvider(const QString &providerId) {
    if (m_palmRuntime->isRunning()) return false;
    if (!m_providerManager->providerById(providerId)) return false;

    // Cascade-delete mappings.
    const QList<int> indices = mappingIndicesFor(providerId);
    if (!indices.isEmpty()) {
        QJsonArray arr = m_profile->syncMappingsJson();
        // Remove from highest index to lowest so positions stay valid.
        QList<int> sorted = indices;
        std::sort(sorted.begin(), sorted.end(), std::greater<int>());
        for (int idx : sorted) arr.removeAt(idx);
        m_profile->setSyncMappingsJson(arr);
        m_profile->save();
        Q_EMIT mappingsChanged();
    }

    m_providerManager->removeProvider(providerId);
    m_states.remove(providerId);
    m_lastErrors.remove(providerId);

    m_profile->removeAccount(providerId);
    m_profile->save();
    return true;
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

QStringList AccountController::mappingDescriptionsFor(const QString &id,
                                                     int max) const {
    QStringList out;
    const QJsonArray arr = m_profile->syncMappingsJson();
    const QString prefix = id + QStringLiteral(":");
    for (int i = 0; i < arr.size() && out.size() < max; ++i) {
        const QJsonObject row = arr.at(i).toObject();
        const QString src = row.value("sourceBackend").toString();
        const QString tgt = row.value("targetBackend").toString();
        if (src.startsWith(prefix) || tgt.startsWith(prefix)) {
            const QString sCol = row.value("sourceCalendar").toString();
            const QString tCol = row.value("targetCalendar").toString();
            out.append(QStringLiteral("%1/%2 \xe2\x86\x92 %3/%4").arg(src, sCol, tgt, tCol));
        }
    }
    return out;
}

ProviderManager *AccountController::providerManager() const {
    return m_providerManager.get();
}

BackendRegistry *AccountController::backendRegistry() const {
    return m_registry;
}

void AccountController::appendMappings(const QJsonArray &rows) {
    if (rows.isEmpty()) return;
    QJsonArray arr = m_profile->syncMappingsJson();
    for (const auto &v : rows) arr.append(v);
    m_profile->setSyncMappingsJson(arr);
    m_profile->save();
    Q_EMIT mappingsChanged();
    if (!m_palmRuntime->isRunning()) {
        m_palmRuntime->reloadMappings(arr);
    }
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
