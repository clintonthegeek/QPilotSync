// tests/runtime/tst_patchbay_view.cpp
#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonObject>

#include "../../src/app/patchbay/patchbaymodel.h"
#include "../../src/app/patchbay/syncpatchbayview.h"
#include "../../src/app/patchbay/signalpathwire.h"
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

} // namespace

class TstPatchbayView : public QObject {
    Q_OBJECT
private slots:
    void rebuildPopulatesScene();
    void columnsAreOrdered();
    void wireCarriesState();
};

void TstPatchbayView::rebuildPopulatesScene()
{
    PatchbayModel model;
    auto in = baseInputs();
    in.mappings.append(row("m1", "calendar", "", "acc-1:cal1", "cal1"));
    model.setInputs(in);

    SyncPatchbayView view;
    view.setModel(&model);

    QCOMPARE(view.nodeCount(), 3);      // palm + hub + remote:acc-1
    QCOMPARE(view.wireCount(), 1);
    QVERIFY(view.strandCount() >= 4);   // 4 whole-domain strands minimum
    QVERIFY(view.wireItem("m1") != nullptr);
}

void TstPatchbayView::columnsAreOrdered()
{
    PatchbayModel model;
    model.setInputs(baseInputs());
    SyncPatchbayView view;
    view.setModel(&model);

    const QPointF palm = view.nodePos("palm");
    const QPointF hub = view.nodePos("hub");
    const QPointF remote = view.nodePos("remote:acc-1");
    QVERIFY(palm.x() < hub.x());
    QVERIFY(hub.x() < remote.x());
}

void TstPatchbayView::wireCarriesState()
{
    PatchbayModel model;
    auto in = baseInputs();
    in.mappings.append(row("m1", "calendar", "", "acc-1:cal1", "cal1",
                           "OneWayUpload"));
    model.setInputs(in);
    SyncPatchbayView view;
    view.setModel(&model);
    QCOMPARE(view.wireItem("m1")->wireState(), WireState::OneWayUpload);
}

WILDPALMS_QTEST_MAIN(TstPatchbayView)
#include "tst_patchbay_view.moc"
