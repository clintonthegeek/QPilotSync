#include <QTest>

#include "contactsdomainextension.h"

#include "domainregistry.h"
#include "transformationregistry.h"

using WildPalms::ContactsPlugin::ContactsDomainExtension;
using namespace Kalburator::Shape;

class TestContactsDomainExtension : public QObject {
    Q_OBJECT
private slots:
    void cleanup()
    {
        DomainRegistry::instance().clear();
        TransformationRegistry::instance().clear();
    }

    void registersPalmShape()
    {
        auto& reg = TransformationRegistry::instance();
        // vcard4 endpoint must exist before edge registration (the
        // libkalburator contacts plugin does this at runtime; in tests
        // we stub it). Without this, registerEdge asserts "to-shape
        // not registered".
        const Shape v4{ DomainId{"contacts"}, EncodingId{"vcard4"} };
        reg.registerShape(v4, {});

        ContactsDomainExtension::registerWith(reg);

        const Shape palm{ DomainId{"contacts"}, EncodingId{"palm"} };
        QVERIFY(reg.catalogueFor(palm) != nullptr);
    }

    void registersBothEdges()
    {
        auto& reg = TransformationRegistry::instance();
        // We need vcard4 registered for the v4 endpoint. The libkalburator
        // contacts plugin does this; in tests we register it ourselves
        // BEFORE calling extension's registerWith (extension creates edges
        // which require both endpoints to already be registered).
        const Shape v4  { DomainId{"contacts"}, EncodingId{"vcard4"} };
        reg.registerShape(v4, {});

        ContactsDomainExtension::registerWith(reg);

        const Shape palm{ DomainId{"contacts"}, EncodingId{"palm"} };

        const auto edgesFromPalm = reg.edgesFrom(palm);
        QVERIFY2(std::any_of(edgesFromPalm.begin(), edgesFromPalm.end(),
                             [&](const auto &e) { return e.to == v4; }),
                 "expected palm -> vcard4 edge");

        const auto edgesFromV4 = reg.edgesFrom(v4);
        QVERIFY2(std::any_of(edgesFromV4.begin(), edgesFromV4.end(),
                             [&](const auto &e) { return e.to == palm; }),
                 "expected vcard4 -> palm edge");
    }

    void idempotentReregister()
    {
        auto& reg = TransformationRegistry::instance();
        const Shape v4{ DomainId{"contacts"}, EncodingId{"vcard4"} };
        reg.registerShape(v4, {});
        ContactsDomainExtension::registerWith(reg);
        ContactsDomainExtension::registerWith(reg);   // must not assert
    }
};

QTEST_GUILESS_MAIN(TestContactsDomainExtension)
#include "tst_contactsdomainextension.moc"
