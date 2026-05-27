#include <QTest>

// WildPalms memo plugin
#include "notedomainextension.h"
#include "palm/sync/palmrecord.h"
#include "palm/codecs/memocodec.h"

// libkalburator shape graph + note canon (via Kalburator::Sync)
#include "shaperegistries.h"
#include "transformationregistry.h"
#include "shape.h"
#include "pipeline.h"
#include "lossprofile.h"
#include "notedomaindefinition.h"
#include "notestockshapes.h"

using namespace Kalburator::Shape;
using WildPalms::Memo::NotePalmShapes;
using WildPalms::PalmSync::PalmRecord;

namespace {

ShapeRegistries makeNoteRegistries()
{
    ShapeRegistries regs;
    auto &reg = regs.transformation;

    Kalburator::Note::NoteDomainDefinition def;
    const auto spine = def.canonicalSpine();   // [ (note, canon) ]
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

    Kalburator::Note::NoteStockShapes stock;
    for (const auto &[shape, cat] : stock.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto &edge : stock.edges())
        reg.registerEdge(edge);

    // WildPalms: (note, palm) + palm<->canon edges. Not part of stock; the
    // palm->canon path only compiles once these run.
    NotePalmShapes wpShapes;
    for (const auto &[shape, cat] : wpShapes.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto &edge : wpShapes.edges())
        reg.registerEdge(edge);

    return regs;
}

// Build a (note, palm) record's wire bytes carrying a known recordId + slot.
QByteArray makePalmMemoBytes(quint8 slot, quint32 recordId, const QString &text)
{
    WildPalms::PalmCodecs::Memo memo;
    memo.text = text;

    PalmRecord pr;
    pr.recordId = recordId;
    pr.category = slot;
    pr.data     = WildPalms::PalmCodecs::encodeMemo(memo);
    return pr.toWireBytes();
}

} // namespace

class TestMemoNoteRoundtrip : public QObject {
    Q_OBJECT
private slots:

    void palmCanonRoundTripPreservesIdentity()
    {
        const auto regs = makeNoteRegistries();
        const Shape palm { DomainId{"note"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"note"}, EncodingId{"canon"} };

        const QByteArray palmBytes =
            makePalmMemoBytes(/*slot*/ 3, /*recordId*/ 88,
                              QStringLiteral("Buy milk\nand eggs"));

        const auto fwd = regs.transformation.compile(palm, canon);
        QVERIFY2(fwd.has_value(),
                 "no palm->canon pipeline (palm->markdown->canon should compile)");
        const QByteArray canonBytes = fwd->apply(palmBytes);
        QVERIFY2(!canonBytes.isEmpty(), "palm->canon produced empty bytes");

        const auto rev = regs.transformation.compile(canon, palm);
        QVERIFY2(rev.has_value(), "no canon->palm pipeline");
        const QByteArray palmBytes2 = rev->apply(canonBytes);
        QVERIFY2(!palmBytes2.isEmpty(), "canon->palm produced empty bytes");

        const PalmRecord pr2 = PalmRecord::fromWireBytes(palmBytes2);
        QCOMPARE(pr2.recordId, 88u);
        QCOMPARE(static_cast<int>(pr2.category), 3);
        const auto decoded = WildPalms::PalmCodecs::decodeMemo(pr2.data);
        QVERIFY2(decoded.has_value(), "round-tripped palm memo did not decode");
        QCOMPARE(decoded->text, QStringLiteral("Buy milk\nand eggs"));
    }

    void lossProfileIsHonest()
    {
        const auto regs = makeNoteRegistries();
        const Shape palm { DomainId{"note"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"note"}, EncodingId{"canon"} };

        // palm->canon is NOT isLossless here: libkalburator's markdown<->canon
        // edge declares frontmatter=Reversible (identity rides in providerExtras),
        // and Reversible is a non-empty affected entry. But nothing is DROPPED.
        const LossProfile up = regs.transformation.inspect(palm, canon);
        QVERIFY2(up.droppedProperties().isEmpty(),
                 "palm->canon must drop nothing");
        QCOMPARE(up.affected.value(PropertyId{QStringLiteral("frontmatter")}),
                 LossKind::Reversible);

        const LossProfile down = regs.transformation.inspect(canon, palm);
        QVERIFY2(!down.isLossless(),
                 "canon->palm must report loss (Palm holds plain text only)");
        QCOMPARE(down.affected.value(PropertyId{QStringLiteral("body")}),
                 LossKind::Simplified);
    }
};

QTEST_GUILESS_MAIN(TestMemoNoteRoundtrip)
#include "tst_memo_note_roundtrip.moc"
