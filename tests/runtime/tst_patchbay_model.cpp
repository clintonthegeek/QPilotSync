// tests/runtime/tst_patchbay_model.cpp
#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>

#include "../../src/app/patchbay/patchbaymodel.h"
#include "../wildpalms_qtest_main.h"

#include <collectioninfo.h>

using namespace WildPalms::AppPatchbay;
using Kalburator::Sync::CollectionInfo;
using WildPalms::Runtime::RouteStatus;

namespace {

QList<ConduitFacts> stockConduits()
{
    auto typeIs = [](const char *t) {
        return [t](const CollectionInfo &c) { return c.type == QLatin1String(t); };
    };
    return {
        { "calendar", "calendar", "DatebookDB", "Calendar", typeIs("calendar") },
        { "contacts", "contacts", "AddressDB",  "Contacts", typeIs("contacts") },
        { "memo",     "note",     "MemoDB",     "Memos",    typeIs("notes")    },
        { "todo",     "todo",     "ToDoDB",     "Todos",    typeIs("todo")     },
    };
}

QStringList snapshot16(std::initializer_list<const char *> names)
{
    QStringList l;
    for (const char *n : names) l << QString::fromUtf8(n);
    while (l.size() < 16) l << QString();
    return l;
}

QJsonObject row(const char *id, const char *srcBackend, const char *srcCal,
                const char *tgtBackend, const char *tgtCal,
                const char *mode = "TwoWay", bool enabled = true)
{
    QJsonObject r;
    r["id"] = id;
    r["sourceBackend"] = srcBackend;
    r["sourceCalendar"] = srcCal;
    r["targetBackend"] = tgtBackend;
    r["targetCalendar"] = tgtCal;
    r["mode"] = mode;
    r["conflictPolicy"] = "LastWriteWins";
    r["enabled"] = enabled;
    return r;
}

PatchbayModel::Inputs baseInputs()
{
    PatchbayModel::Inputs in;
    in.conduits = stockConduits();
    in.slotSnapshot = {
        { "DatebookDB", snapshot16({"Unfiled", "Work", "Personal"}) },
        { "AddressDB",  snapshot16({"Unfiled"}) },
    };
    in.desiredCategories = { { "DatebookDB", {"Work", "Personal"} } };
    CollectionInfo cal;
    cal.id = "cal1"; cal.name = "Team"; cal.type = "calendar";
    PatchbayModel::ProviderEntry nc{ "acc-1", "Nextcloud", QString(), { cal } };
    in.providers = { nc };
    in.deviceConnected = true;
    in.deviceName = "Palm m515";
    return in;
}

const NodeDesc *nodeById(const QList<NodeDesc> &nodes, const QString &id)
{
    for (const auto &n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

const BandDesc *bandByDomain(const NodeDesc &n, const QString &domain)
{
    for (const auto &b : n.bands)
        if (b.domain == domain) return &b;
    return nullptr;
}

} // namespace

class TstPatchbayModel : public QObject {
    Q_OBJECT
private slots:
    // Task 6
    void hubHasBandPerConduit();
    void hubBandPortsOrderedWholeThenCategoriesThenAdd();
    void hubCategoriesUnionDesiredAndRows();
    void palmNodeBandsAndSlots();
    void disconnectedDeviceGhostsPalmNode();
};

void TstPatchbayModel::hubHasBandPerConduit()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    const auto *hub = nodeById(m.nodes(), "hub");
    QVERIFY(hub);
    QCOMPARE(hub->kind, NodeKind::Hub);
    QCOMPARE(hub->bands.size(), 4);          // descriptor-driven
    QVERIFY(bandByDomain(*hub, "calendar"));
    QVERIFY(bandByDomain(*hub, "note"));     // memo's domain is "note"
}

void TstPatchbayModel::hubBandPortsOrderedWholeThenCategoriesThenAdd()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    const auto *band = bandByDomain(*nodeById(m.nodes(), "hub"), "calendar");
    QVERIFY(band);
    QVERIFY(band->ports.size() >= 4);        // All + Work + Personal + add
    QCOMPARE(band->ports.first().kind, PortKind::WholeDomain);
    QCOMPARE(band->ports.first().id, QStringLiteral("dom:calendar"));
    QCOMPARE(band->ports[1].id, QStringLiteral("cat:calendar/Work"));
    QCOMPARE(band->ports.last().kind, PortKind::AddCategory);
}

void TstPatchbayModel::hubCategoriesUnionDesiredAndRows()
{
    auto in = baseInputs();
    // a row referencing a category NOT in desiredCategories must still get a port
    in.mappings.append(row("m1", "calendar", "palm:calendar/name:Conferences",
                           "acc-1:cal1", "cal1"));
    PatchbayModel m;
    m.setInputs(in);
    const auto *band = bandByDomain(*nodeById(m.nodes(), "hub"), "calendar");
    bool found = false;
    for (const auto &p : band->ports)
        if (p.id == QStringLiteral("cat:calendar/Conferences")) found = true;
    QVERIFY(found);
}

void TstPatchbayModel::palmNodeBandsAndSlots()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    const auto *palm = nodeById(m.nodes(), "palm");
    QVERIFY(palm);
    QCOMPARE(palm->kind, NodeKind::Palm);
    QVERIFY(!palm->ghosted);
    QCOMPARE(palm->title, QStringLiteral("Palm m515"));
    const auto *db = bandByDomain(*palm, "calendar");
    QVERIFY(db);
    // ports: slot 0 Unfiled, slot 1 Work, slot 2 Personal (empty slots skipped)
    QCOMPARE(db->ports.size(), 3);
    QCOMPARE(db->ports[1].id, QStringLiteral("slot:DatebookDB/1"));
    QCOMPARE(db->ports[1].label, QStringLiteral("Work"));
}

void TstPatchbayModel::disconnectedDeviceGhostsPalmNode()
{
    auto in = baseInputs();
    in.deviceConnected = false;
    PatchbayModel m;
    m.setInputs(in);
    const auto *palm = nodeById(m.nodes(), "palm");
    QVERIFY(palm);
    QVERIFY(palm->ghosted);                  // node never vanishes (spec §5.1)
    QVERIFY(!palm->bands.isEmpty());         // snapshot still rendered
}

WILDPALMS_QTEST_MAIN(TstPatchbayModel)
#include "tst_patchbay_model.moc"
