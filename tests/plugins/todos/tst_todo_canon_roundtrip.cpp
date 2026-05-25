#include <QTest>

// WildPalms todo plugin
#include "tododomainextension.h"
#include "palm/sync/palmrecord.h"
#include "palm/codecs/todocodec.h"          // Todo POD, encodeTodo/decodeTodo

// libkalburator shape graph + todo canon (via Kalburator::Sync)
#include "shaperegistries.h"
#include "transformationregistry.h"
#include "shape.h"
#include "pipeline.h"
#include "lossprofile.h"
#include "tododomaindefinition.h"
#include "todostockshapes.h"

using namespace Kalburator::Shape;
using WildPalms::TodoPlugin::TodoDomainExtension;
using WildPalms::PalmSync::PalmRecord;
using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCodecs::encodeTodo;
using WildPalms::PalmCodecs::decodeTodo;

namespace {

ShapeRegistries makeTodoRegistries()
{
    ShapeRegistries regs;
    auto &reg = regs.transformation;

    Kalburator::Todo::TodoDomainDefinition def;
    const auto spine = def.canonicalSpine();
    if (!spine.isEmpty()) {
        const auto &[rootShape, rootCat] = spine.first();
        reg.registerShape(rootShape, rootCat);
        reg.declareCanonical(def.domain(), rootShape);
        for (int i = 1; i < spine.size(); ++i) {
            const auto &[s, cat] = spine.at(i);
            reg.registerShape(s, cat);
            reg.appendCanonicalVersion(def.domain(), s);
        }
    }

    Kalburator::Todo::TodoStockShapes stock;
    for (const auto &[shape, cat] : stock.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto &edge : stock.edges())
        reg.registerEdge(edge);

    // WildPalms: (todo, palm) + palm<->ical-vtodo edges. Not part of stock; the
    // palm->ical-vtodo->canon path only compiles once this runs.
    TodoDomainExtension::registerWith(reg);

    return regs;
}

// Build a (todo, palm) record's wire bytes carrying a known recordId + content.
// The recordId identity stamp (X-WP-PALM-RECORDID) is applied by the transcoder
// (encodePalmToIcs reads it from PalmRecord::recordId), so we set it here.
QByteArray makePalmTodoBytes(quint8 slot, quint32 recordId,
                             const QString &description)
{
    Todo t;
    t.description = description;
    t.priority    = 1;        // highest
    t.isComplete  = false;

    PalmRecord pr;
    pr.recordId = recordId;
    pr.category = slot;
    pr.data     = encodeTodo(t);
    return pr.toWireBytes();
}

} // namespace

class TestTodoCanonRoundtrip : public QObject {
    Q_OBJECT
private slots:

    void palmCanonRoundTripPreservesIdentity()
    {
        const auto regs = makeTodoRegistries();
        const Shape palm { DomainId{"todo"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"todo"}, EncodingId{"canon"} };

        const QByteArray palmBytes =
            makePalmTodoBytes(/*slot*/ 2, /*recordId*/ 77,
                              QStringLiteral("Buy milk"));

        const auto fwd = regs.transformation.compile(palm, canon);
        QVERIFY2(fwd.has_value(),
                 "no palm->canon pipeline (palm->ical-vtodo->canon should compile)");
        const QByteArray canonBytes = fwd->apply(palmBytes);
        QVERIFY2(!canonBytes.isEmpty(), "palm->canon produced empty bytes");

        const auto rev = regs.transformation.compile(canon, palm);
        QVERIFY2(rev.has_value(), "no canon->palm pipeline");
        const QByteArray palmBytes2 = rev->apply(canonBytes);
        QVERIFY2(!palmBytes2.isEmpty(), "canon->palm produced empty bytes");

        // Decode the round-tripped palm wire and check summary + recordId survive.
        const PalmRecord pr2 = PalmRecord::fromWireBytes(palmBytes2);
        QCOMPARE(pr2.recordId, 77u);
        const auto decoded = decodeTodo(QByteArrayView(pr2.data));
        QVERIFY2(decoded.has_value(), "round-tripped palm todo did not decode");
        QCOMPARE(decoded->description, QStringLiteral("Buy milk"));
    }

    void lossProfileIsHonest()
    {
        const auto regs = makeTodoRegistries();
        const Shape palm { DomainId{"todo"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"todo"}, EncodingId{"canon"} };

        QVERIFY2(regs.transformation.inspect(palm, canon).isLossless(),
                 "palm->canon should be lossless");

        const LossProfile down = regs.transformation.inspect(canon, palm);
        QVERIFY2(!down.isLossless(),
                 "canon->palm must report loss (Palm cannot hold most todo fields)");
        QCOMPARE(down.affected.value(PropertyId{QStringLiteral("recurrence")}),
                 LossKind::Dropped);
        QCOMPARE(down.affected.value(PropertyId{QStringLiteral("priority")}),
                 LossKind::Degraded);
    }
};

QTEST_GUILESS_MAIN(TestTodoCanonRoundtrip)
#include "tst_todo_canon_roundtrip.moc"
