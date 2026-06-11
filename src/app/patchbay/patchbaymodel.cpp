// src/app/patchbay/patchbaymodel.cpp
#include "patchbaymodel.h"

#include <QJsonObject>
#include <QUuid>

using Kalburator::Sync::CollectionInfo;
using WildPalms::Runtime::RouteStatus;

namespace WildPalms::AppPatchbay {

namespace {
QString categoryFromSourceCalendar(const QString &sourceCalendar,
                                   const QString &domain)
{
    const QString prefix =
        QStringLiteral("palm:%1/name:").arg(domain);
    if (sourceCalendar.startsWith(prefix))
        return sourceCalendar.mid(prefix.size());
    return QString();
}
} // namespace

PatchbayModel::PatchbayModel(QObject *parent) : QObject(parent) {}

void PatchbayModel::setInputs(const Inputs &inputs)
{
    m_inputs = inputs;
    rebuild();
}

const ConduitFacts *PatchbayModel::conduitForDomain(const QString &domain) const
{
    for (const auto &c : m_inputs.conduits)
        if (c.domain == domain) return &c;
    return nullptr;
}

const ConduitFacts *PatchbayModel::conduitForId(const QString &conduitId) const
{
    for (const auto &c : m_inputs.conduits)
        if (c.conduitId == conduitId) return &c;
    return nullptr;
}

QStringList PatchbayModel::hubCategoryNames(const ConduitFacts &c) const
{
    QStringList names = m_inputs.desiredCategories.value(c.dbName);
    auto containsCi = [&names](const QString &n) {
        for (const auto &x : names)
            if (x.compare(n, Qt::CaseInsensitive) == 0) return true;
        return false;
    };
    for (const auto &v : m_inputs.mappings) {
        const QJsonObject r = v.toObject();
        if (r.value(QLatin1String("sourceBackend")).toString() != c.conduitId)
            continue;
        const QString cat = categoryFromSourceCalendar(
            r.value(QLatin1String("sourceCalendar")).toString(), c.domain);
        if (!cat.isEmpty() && !containsCi(cat)
            && cat.compare(QLatin1String("Unfiled"), Qt::CaseInsensitive) != 0)
            names << cat;
    }
    names.removeAll(QString());
    return names;
}

void PatchbayModel::rebuild()
{
    m_nodes.clear();
    m_wires.clear();
    m_strands.clear();

    // ── Palm node ────────────────────────────────────────────────────
    NodeDesc palm;
    palm.id = QStringLiteral("palm");
    palm.kind = NodeKind::Palm;
    palm.title = m_inputs.deviceName.isEmpty()
        ? QStringLiteral("Palm device") : m_inputs.deviceName;
    palm.subtitle = m_inputs.deviceConnected
        ? QStringLiteral("connected") : QStringLiteral("disconnected");
    palm.ghosted = !m_inputs.deviceConnected;
    for (const auto &c : m_inputs.conduits) {
        BandDesc band;
        band.domain = c.domain;
        band.title = QStringLiteral("%1 — %2").arg(c.displayName, c.dbName);
        const QStringList snap = m_inputs.slotSnapshot.value(c.dbName);
        for (int i = 0; i < snap.size(); ++i) {
            if (snap[i].isEmpty())
                continue;
            PortDesc p;
            p.id = QStringLiteral("slot:%1/%2").arg(c.dbName).arg(i);
            p.label = snap[i];
            p.domain = c.domain;
            p.kind = PortKind::PalmSlot;
            band.ports << p;
        }
        palm.bands << band;
    }
    m_nodes << palm;

    // ── Hub node ─────────────────────────────────────────────────────
    NodeDesc hub;
    hub.id = QStringLiteral("hub");
    hub.kind = NodeKind::Hub;
    hub.title = QStringLiteral("HUB");
    for (const auto &c : m_inputs.conduits) {
        BandDesc band;
        band.domain = c.domain;
        band.title = c.displayName;
        PortDesc whole;
        whole.id = QStringLiteral("dom:%1").arg(c.domain);
        whole.label = QStringLiteral("All %1").arg(c.displayName.toLower());
        whole.domain = c.domain;
        whole.kind = PortKind::WholeDomain;
        band.ports << whole;
        for (const QString &cat : hubCategoryNames(c)) {
            PortDesc p;
            p.id = QStringLiteral("cat:%1/%2").arg(c.domain, cat);
            p.label = cat;
            p.domain = c.domain;
            p.kind = PortKind::Category;
            band.ports << p;
        }
        PortDesc add;
        add.id = QStringLiteral("add:%1").arg(c.domain);
        add.label = QStringLiteral("+ category…");
        add.domain = c.domain;
        add.kind = PortKind::AddCategory;
        band.ports << add;
        hub.bands << band;
    }
    m_nodes << hub;

    rebuildRemotes();           // Task 7
    rebuildWiresAndStrands();   // Task 8

    emit rebuilt();
}

void PatchbayModel::rebuildRemotes()
{
    // Real providers
    for (const auto &prov : m_inputs.providers) {
        NodeDesc n;
        n.id = QStringLiteral("remote:%1").arg(prov.providerId);
        n.kind = NodeKind::Remote;
        n.title = prov.displayName;
        n.subtitle = prov.busyText;
        BandDesc band;
        if (prov.busyText.isEmpty()) {
            for (const auto &col : prov.collections) {
                for (const auto &c : m_inputs.conduits) {
                    if (!c.matchesCollection || !c.matchesCollection(col))
                        continue;
                    PortDesc p;
                    p.id = QStringLiteral("col:%1|%2").arg(col.id, c.domain);
                    p.label = col.name;
                    p.domain = c.domain;
                    p.kind = PortKind::RemoteCollection;
                    band.ports << p;
                }
            }
        }
        n.bands << band;
        m_nodes << n;
    }

    // Ghost remotes: rows whose targetBackend references an unknown provider
    // or a provider that lacks the collection. targetBackend is
    // "<providerId>:<collectionId>"; providerId never contains ':' (it is an
    // account uuid), so split on the FIRST ':' only.
    for (const auto &v : m_inputs.mappings) {
        const QJsonObject r = v.toObject();
        const QString target = r.value(QLatin1String("targetBackend")).toString();
        const int colon = target.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString providerId = target.left(colon);
        const QString collectionId = target.mid(colon + 1);
        const ConduitFacts *c =
            conduitForId(r.value(QLatin1String("sourceBackend")).toString());
        if (!c)
            continue;

        bool resolved = false;
        for (const auto &prov : m_inputs.providers) {
            if (prov.providerId != providerId)
                continue;
            if (!prov.busyText.isEmpty()) { resolved = true; break; } // pending, not broken
            for (const auto &col : prov.collections)
                if (col.id == collectionId) { resolved = true; break; }
            break;
        }
        if (resolved)
            continue;

        const QString ghostId = QStringLiteral("ghost:%1").arg(providerId);
        NodeDesc *ghost = nullptr;
        for (auto &n : m_nodes)
            if (n.id == ghostId) ghost = &n;
        if (!ghost) {
            NodeDesc n;
            n.id = ghostId;
            n.kind = NodeKind::GhostRemote;
            n.title = QStringLiteral("Missing account");
            n.subtitle = providerId;
            n.ghosted = true;
            n.bands << BandDesc{};
            m_nodes << n;
            ghost = &m_nodes.last();
        }
        const QString portId = QStringLiteral("col:%1|%2").arg(collectionId, c->domain);
        bool havePort = false;
        for (const auto &p : ghost->bands.first().ports)
            if (p.id == portId) havePort = true;
        if (!havePort) {
            PortDesc p;
            p.id = portId;
            p.label = collectionId;
            p.domain = c->domain;
            p.kind = PortKind::RemoteCollection;
            ghost->bands.first().ports << p;
        }
    }
}

void PatchbayModel::rebuildWiresAndStrands() {}   // Task 8

// Edit ops — real bodies land in Task 9; stubs keep the lib linking.
QString PatchbayModel::addMapping(const QString &, const QString &,
                                  const QString &)
{
    return {};
}

bool PatchbayModel::removeMapping(const QString &) { return false; }
bool PatchbayModel::updateMapping(const QString &, const QJsonObject &) { return false; }
bool PatchbayModel::addCategory(const QString &, const QString &) { return false; }
bool PatchbayModel::removeCategory(const QString &, const QString &) { return false; }

QJsonObject PatchbayModel::mappingById(const QString &mappingId) const
{
    for (const auto &v : m_inputs.mappings) {
        const QJsonObject r = v.toObject();
        if (r.value(QLatin1String("id")).toString() == mappingId)
            return r;
    }
    return {};
}

WildPalms::Runtime::RouteStatus
PatchbayModel::statusFor(const QString &mappingId) const
{
    return m_inputs.routeStatuses.value(mappingId, RouteStatus::Active);
}

} // namespace WildPalms::AppPatchbay
