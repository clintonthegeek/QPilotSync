// tests/runtime/tst_domainfilter.cpp
#include <QtTest/QtTest>
#include "../wildpalms_qtest_main.h"

#include "app/wizard/domainfilter.h"
#include <collectioninfo.h>

using Kalburator::Sync::CollectionInfo;
using WildPalms::Wizard::collectionMatchesDomain;

class TstDomainFilter : public QObject {
    Q_OBJECT
private slots:
    void matchesByCollectionType();
    void matchesByContentTypesFallback();
    void vtodoCalendarServesBothCalendarAndTodo();
    void unknownPluginMatchesNothing();
};

void TstDomainFilter::matchesByCollectionType()
{
    CollectionInfo c;
    c.type = QStringLiteral("calendar");
    QVERIFY(collectionMatchesDomain(c, QStringLiteral("calendar")));
    QVERIFY(!collectionMatchesDomain(c, QStringLiteral("contacts")));

    c.type = QStringLiteral("contacts");
    QVERIFY(collectionMatchesDomain(c, QStringLiteral("contacts")));

    c.type = QStringLiteral("todos");
    QVERIFY(collectionMatchesDomain(c, QStringLiteral("todo")));

    c.type = QStringLiteral("memos");
    QVERIFY(collectionMatchesDomain(c, QStringLiteral("memo")));
    QVERIFY(!collectionMatchesDomain(c, QStringLiteral("calendar")));
}

void TstDomainFilter::matchesByContentTypesFallback()
{
    CollectionInfo c;   // no type set — DAV servers may only report contentTypes
    c.contentTypes = { QStringLiteral("VEVENT") };
    QVERIFY(collectionMatchesDomain(c, QStringLiteral("calendar")));
    QVERIFY(!collectionMatchesDomain(c, QStringLiteral("todo")));

    c.contentTypes = { QStringLiteral("VCARD") };
    QVERIFY(collectionMatchesDomain(c, QStringLiteral("contacts")));
}

void TstDomainFilter::vtodoCalendarServesBothCalendarAndTodo()
{
    CollectionInfo c;
    c.type = QStringLiteral("calendar");
    c.contentTypes = { QStringLiteral("VEVENT"), QStringLiteral("VTODO") };
    QVERIFY(collectionMatchesDomain(c, QStringLiteral("calendar")));
    QVERIFY(collectionMatchesDomain(c, QStringLiteral("todo")));
}

void TstDomainFilter::unknownPluginMatchesNothing()
{
    CollectionInfo c;
    c.type = QStringLiteral("calendar");
    QVERIFY(!collectionMatchesDomain(c, QStringLiteral("plucker")));
}

WILDPALMS_QTEST_MAIN(TstDomainFilter)
#include "tst_domainfilter.moc"
