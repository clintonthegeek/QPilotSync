#include <QtTest/QtTest>
#include <QHash>

#include "runtime/routemapping.h"
#include "palm/calendar/categorymappingstore.h"
#include "synctypes.h"

using WildPalms::Runtime::RouteSpec;
using WildPalms::Runtime::translateRouteSpec;
using WildPalms::PalmCalendar::CategoryMappingStore;
using Kalburator::Sync::SyncMapping;

class TestRouteMapping : public QObject { Q_OBJECT
private slots:
    void slotMapped_yieldsFilteredRoute()
    {
        CategoryMappingStore cats;
        cats.setSlotName(QStringLiteral("DatebookDB"), 3, QStringLiteral("Work"));
        QHash<QString, CategoryMappingStore*> stores{{QStringLiteral("calendar"), &cats}};

        SyncMapping persisted;
        persisted.id             = QStringLiteral("user-cal-work");
        persisted.sourceBackend  = QStringLiteral("calendar");
        persisted.sourceCalendar = QStringLiteral("palm:calendar/3");
        persisted.targetBackend  = QStringLiteral("caldav-uuid");
        persisted.targetCalendar = QStringLiteral("WorkCal");
        persisted.enabled        = true;

        const auto specOpt = translateRouteSpec(persisted, stores);
        QVERIFY(specOpt.has_value());
        const auto &s = *specOpt;
        QCOMPARE(s.kind,                   RouteSpec::Kind::Filtered);
        QCOMPARE(s.domain,                 QStringLiteral("calendar"));
        QCOMPARE(s.hubCollectionId,        QStringLiteral("calendar"));
        QCOMPARE(s.categoryName,           QStringLiteral("Work"));
        QCOMPARE(s.remoteBackendId,        QStringLiteral("caldav-uuid"));
        QCOMPARE(s.remoteCollectionId,     QStringLiteral("WorkCal"));
        QCOMPARE(s.lcId,                   QStringLiteral("wp-route-user-cal-work"));
    }

    void wildcardSource_yieldsDirectRoute()
    {
        QHash<QString, CategoryMappingStore*> stores;

        SyncMapping persisted;
        persisted.id             = QStringLiteral("user-cal-all");
        persisted.sourceBackend  = QStringLiteral("calendar");
        persisted.sourceCalendar = QString();
        persisted.targetBackend  = QStringLiteral("caldav-uuid");
        persisted.targetCalendar = QStringLiteral("Personal");
        persisted.enabled        = true;

        const auto specOpt = translateRouteSpec(persisted, stores);
        QVERIFY(specOpt.has_value());
        QCOMPARE(specOpt->kind,            RouteSpec::Kind::Direct);
        QCOMPARE(specOpt->domain,          QStringLiteral("calendar"));
        QCOMPARE(specOpt->hubCollectionId, QStringLiteral("calendar"));
        QCOMPARE(specOpt->categoryName,    QString());
        QCOMPARE(specOpt->remoteCollectionId, QStringLiteral("Personal"));
    }

    void slotMapped_unknownSlotName_returnsNullopt()
    {
        CategoryMappingStore cats;
        QHash<QString, CategoryMappingStore*> stores{{QStringLiteral("calendar"), &cats}};

        SyncMapping persisted;
        persisted.id             = QStringLiteral("user-cal-7");
        persisted.sourceBackend  = QStringLiteral("calendar");
        persisted.sourceCalendar = QStringLiteral("palm:calendar/7");
        persisted.targetBackend  = QStringLiteral("caldav-uuid");
        persisted.targetCalendar = QStringLiteral("Foo");
        persisted.enabled        = true;

        QCOMPARE(translateRouteSpec(persisted, stores), std::nullopt);
    }

    void disabledPersisted_returnsNullopt()
    {
        QHash<QString, CategoryMappingStore*> stores;
        SyncMapping persisted;
        persisted.sourceBackend  = QStringLiteral("calendar");
        persisted.sourceCalendar = QString();
        persisted.targetBackend  = QStringLiteral("caldav-uuid");
        persisted.targetCalendar = QStringLiteral("Personal");
        persisted.enabled        = false;
        QCOMPARE(translateRouteSpec(persisted, stores), std::nullopt);
    }

    void unknownPalmDomain_returnsNullopt()
    {
        QHash<QString, CategoryMappingStore*> stores;
        SyncMapping persisted;
        persisted.sourceBackend  = QStringLiteral("not-a-palm-plugin");
        persisted.sourceCalendar = QStringLiteral("palm:???/3");
        persisted.targetBackend  = QStringLiteral("caldav-uuid");
        persisted.targetCalendar = QStringLiteral("X");
        persisted.enabled        = true;
        QCOMPARE(translateRouteSpec(persisted, stores), std::nullopt);
    }
};
QTEST_GUILESS_MAIN(TestRouteMapping)
#include "tst_route_mapping.moc"
