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

void PatchbayModel::rebuildWiresAndStrands()
{
    // ── Wires: one per persisted row ─────────────────────────────────
    for (const auto &v : m_inputs.mappings) {
        const QJsonObject r = v.toObject();
        const QString id = r.value(QLatin1String("id")).toString();
        const ConduitFacts *c =
            conduitForId(r.value(QLatin1String("sourceBackend")).toString());
        if (!c)
            continue;   // unknown conduit: row is invisible only if we can't
                        // even determine a domain — covered by NotARoute UI in
                        // the inspector; ghost nodes need a domain to draw.

        WireDesc w;
        w.mappingId = id;
        w.domain = c->domain;

        const QString srcCal =
            r.value(QLatin1String("sourceCalendar")).toString();
        const QString cat = categoryFromSourceCalendar(srcCal, c->domain);
        w.sourcePortId = cat.isEmpty()
            ? QStringLiteral("dom:%1").arg(c->domain)
            : QStringLiteral("cat:%1/%2").arg(c->domain, cat);

        const QString target = r.value(QLatin1String("targetBackend")).toString();
        const int colon = target.indexOf(QLatin1Char(':'));
        const QString providerId = colon > 0 ? target.left(colon) : QString();
        const QString collectionId = colon > 0 ? target.mid(colon + 1) : QString();
        w.targetPortId = QStringLiteral("col:%1|%2").arg(collectionId, c->domain);

        // resolve target node: real remote if the port exists there, else ghost
        const QString remoteId = QStringLiteral("remote:%1").arg(providerId);
        const QString ghostId = QStringLiteral("ghost:%1").arg(providerId);
        bool onRemote = false;
        for (const auto &n : m_nodes) {
            if (n.id != remoteId) continue;
            for (const auto &p : n.bands.first().ports)
                if (p.id == w.targetPortId) onRemote = true;
        }
        w.targetNodeId = onRemote ? remoteId : ghostId;

        // state precedence: disabled > broken > mode
        const bool enabled = r.value(QLatin1String("enabled")).toBool(true);
        const QString mode = r.value(QLatin1String("mode")).toString();
        const RouteStatus st = m_inputs.routeStatuses.value(id, RouteStatus::Active);
        if (!enabled || mode == QLatin1String("Disabled"))
            w.state = WireState::Disabled;
        else if (st == RouteStatus::NotARoute || !onRemote)
            w.state = WireState::Broken;
        else if (mode == QLatin1String("OneWayUpload"))
            w.state = WireState::OneWayUpload;
        else if (mode == QLatin1String("OneWayDownload"))
            w.state = WireState::OneWayDownload;
        else
            w.state = WireState::TwoWay;

        if (w.state == WireState::Broken)
            w.beadGlyph = QStringLiteral("✗");

        m_wires << w;
    }

    // ── Strands: system-drawn palm↔hub legs ──────────────────────────
    for (const auto &c : m_inputs.conduits) {
        const QStringList snap = m_inputs.slotSnapshot.value(c.dbName);

        StrandDesc whole;
        whole.hubPortId = QStringLiteral("dom:%1").arg(c.domain);
        whole.id = QStringLiteral("strand:%1").arg(whole.hubPortId);
        whole.palmPortId = QStringLiteral("db:%1").arg(c.dbName);
        whole.domain = c.domain;
        whole.state = StrandState::Solid;
        whole.wholeDomain = true;
        m_strands << whole;

        for (const QString &cat : hubCategoryNames(c)) {
            const QString hubPort = QStringLiteral("cat:%1/%2").arg(c.domain, cat);

            // NoFreeSlot: any row on this category reporting it suppresses the
            // strand and flags the port (set below on the hub NodeDesc).
            bool noSlot = false;
            for (const auto &v : m_inputs.mappings) {
                const QJsonObject r = v.toObject();
                if (r.value(QLatin1String("sourceBackend")).toString() != c.conduitId)
                    continue;
                if (categoryFromSourceCalendar(
                        r.value(QLatin1String("sourceCalendar")).toString(),
                        c.domain).compare(cat, Qt::CaseInsensitive) != 0)
                    continue;
                const QString id = r.value(QLatin1String("id")).toString();
                if (m_inputs.routeStatuses.value(id, RouteStatus::Active)
                    == RouteStatus::NoFreeSlot)
                    noSlot = true;
            }

            int slotIdx = -1;
            for (int i = 0; i < snap.size(); ++i)
                if (snap[i].compare(cat, Qt::CaseInsensitive) == 0) slotIdx = i;

            if (noSlot) {
                for (auto &n : m_nodes) {
                    if (n.kind != NodeKind::Hub) continue;
                    for (auto &b : n.bands)
                        for (auto &p : b.ports)
                            if (p.id == hubPort) p.noFreeSlot = true;
                }
                continue;   // no strand
            }

            StrandDesc s;
            s.hubPortId = hubPort;
            s.id = QStringLiteral("strand:%1").arg(hubPort);
            s.domain = c.domain;
            if (slotIdx >= 0) {
                s.state = StrandState::Solid;
                s.palmPortId = QStringLiteral("slot:%1/%2").arg(c.dbName).arg(slotIdx);
            } else {
                s.state = StrandState::Ghost;     // WaitingForDevice
                s.palmPortId = QStringLiteral("db:%1").arg(c.dbName);
                for (auto &n : m_nodes) {
                    if (n.kind != NodeKind::Hub) continue;
                    for (auto &b : n.bands)
                        for (auto &p : b.ports)
                            if (p.id == hubPort) p.waiting = true;
                }
            }
            m_strands << s;
        }
    }
}

QString PatchbayModel::addMapping(const QString &hubPortId,
                                  const QString &providerId,
                                  const QString &collectionId)
{
    // parse hub port → domain + optional category
    QString domain, category;
    if (hubPortId.startsWith(QLatin1String("dom:"))) {
        domain = hubPortId.mid(4);
    } else if (hubPortId.startsWith(QLatin1String("cat:"))) {
        const QString rest = hubPortId.mid(4);
        const int slash = rest.indexOf(QLatin1Char('/'));
        if (slash <= 0) return {};
        domain = rest.left(slash);
        category = rest.mid(slash + 1);
    } else {
        return {};
    }
    const ConduitFacts *c = conduitForDomain(domain);
    if (!c)
        return {};

    // target must exist and match the conduit's domain rules
    const Kalburator::Sync::CollectionInfo *col = nullptr;
    for (const auto &prov : m_inputs.providers) {
        if (prov.providerId != providerId) continue;
        for (const auto &x : prov.collections)
            if (x.id == collectionId) col = &x;
    }
    if (!col || !c->matchesCollection || !c->matchesCollection(*col))
        return {};

    const QString sourceCalendar = category.isEmpty()
        ? QString()
        : QStringLiteral("palm:%1/name:%2").arg(domain, category);
    const QString targetBackend =
        QStringLiteral("%1:%2").arg(providerId, collectionId);

    // duplicate guard (same source slice → same target)
    for (const auto &v : m_inputs.mappings) {
        const QJsonObject r = v.toObject();
        if (r.value(QLatin1String("sourceBackend")).toString() == c->conduitId
            && r.value(QLatin1String("sourceCalendar")).toString() == sourceCalendar
            && r.value(QLatin1String("targetBackend")).toString() == targetBackend
            && r.value(QLatin1String("targetCalendar")).toString() == collectionId)
            return {};
    }

    QJsonObject row;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    row[QLatin1String("id")] = id;
    row[QLatin1String("sourceBackend")] = c->conduitId;
    row[QLatin1String("sourceCalendar")] = sourceCalendar;
    row[QLatin1String("targetBackend")] = targetBackend;
    row[QLatin1String("targetCalendar")] = collectionId;
    row[QLatin1String("mode")] = QStringLiteral("TwoWay");
    row[QLatin1String("conflictPolicy")] = QStringLiteral("LastWriteWins");
    row[QLatin1String("enabled")] = true;
    m_inputs.mappings.append(row);

    rebuild();
    emit mappingsChanged(m_inputs.mappings);
    return id;
}

bool PatchbayModel::removeMapping(const QString &mappingId)
{
    for (int i = 0; i < m_inputs.mappings.size(); ++i) {
        if (m_inputs.mappings[i].toObject()
                .value(QLatin1String("id")).toString() != mappingId)
            continue;
        m_inputs.mappings.removeAt(i);
        rebuild();
        emit mappingsChanged(m_inputs.mappings);
        return true;
    }
    return false;
}

bool PatchbayModel::updateMapping(const QString &mappingId,
                                  const QJsonObject &changes)
{
    for (int i = 0; i < m_inputs.mappings.size(); ++i) {
        QJsonObject r = m_inputs.mappings[i].toObject();
        if (r.value(QLatin1String("id")).toString() != mappingId)
            continue;
        for (auto it = changes.begin(); it != changes.end(); ++it)
            r[it.key()] = it.value();
        m_inputs.mappings[i] = r;
        rebuild();
        emit mappingsChanged(m_inputs.mappings);
        return true;
    }
    return false;
}

bool PatchbayModel::addCategory(const QString &domain, const QString &name)
{
    const ConduitFacts *c = conduitForDomain(domain);
    const QString trimmed = name.trimmed();
    if (!c || trimmed.isEmpty()
        || trimmed.compare(QLatin1String("Unfiled"), Qt::CaseInsensitive) == 0)
        return false;
    QStringList names = m_inputs.desiredCategories.value(c->dbName);
    for (const auto &x : names)
        if (x.compare(trimmed, Qt::CaseInsensitive) == 0) return false;
    if (names.size() >= 15)   // 16 slots, Unfiled implicit at 0
        return false;
    names << trimmed;
    m_inputs.desiredCategories[c->dbName] = names;
    rebuild();
    emit desiredCategoriesChanged(c->dbName, names);
    return true;
}

bool PatchbayModel::removeCategory(const QString &domain, const QString &name)
{
    const ConduitFacts *c = conduitForDomain(domain);
    if (!c)
        return false;
    // refuse while any row references the category (spec §7.4)
    for (const auto &v : m_inputs.mappings) {
        const QJsonObject r = v.toObject();
        if (r.value(QLatin1String("sourceBackend")).toString() != c->conduitId)
            continue;
        if (categoryFromSourceCalendar(
                r.value(QLatin1String("sourceCalendar")).toString(), domain)
                .compare(name, Qt::CaseInsensitive) == 0)
            return false;
    }
    QStringList names = m_inputs.desiredCategories.value(c->dbName);
    bool removed = false;
    for (int i = names.size() - 1; i >= 0; --i) {
        if (names[i].compare(name, Qt::CaseInsensitive) == 0) {
            names.removeAt(i);
            removed = true;
        }
    }
    if (!removed)
        return false;
    m_inputs.desiredCategories[c->dbName] = names;
    rebuild();
    emit desiredCategoriesChanged(c->dbName, names);
    return true;
}

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
