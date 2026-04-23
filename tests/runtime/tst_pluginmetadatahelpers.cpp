#include <QtTest/QtTest>

#include <KPluginMetaData>
#include <QJsonObject>

#include "runtime/pluginmetadatahelpers.h"

using namespace WildPalms::Runtime;

class TestPluginMetadataHelpers : public QObject
{
    Q_OBJECT
private slots:
    void missingStringReturnsEmpty();
    void presentStringRoundTrips();
    void boolTrueCaseInsensitive();
    void boolFalsyFallsBack();
    void intValidRoundTrips();
    void intInvalidFallsBack();
    void stringListArrayReadsAll();
    void stringListSingleStringWrapsInList();
    void stringListMissingReturnsEmpty();

private:
    static KPluginMetaData metaFor(const QJsonObject &obj)
    {
        return KPluginMetaData(obj, QStringLiteral("synthetic"));
    }
};

void TestPluginMetadataHelpers::missingStringReturnsEmpty()
{
    const KPluginMetaData md = metaFor({});
    QCOMPARE(metaString(md, QStringLiteral("X-Foo")), QString());
}

void TestPluginMetadataHelpers::presentStringRoundTrips()
{
    const KPluginMetaData md = metaFor({{QStringLiteral("X-Foo"), QStringLiteral("bar")}});
    QCOMPARE(metaString(md, QStringLiteral("X-Foo")), QStringLiteral("bar"));
}

void TestPluginMetadataHelpers::boolTrueCaseInsensitive()
{
    const KPluginMetaData md = metaFor({{QStringLiteral("X-Enabled"), QStringLiteral("TRUE")}});
    QVERIFY(metaBool(md, QStringLiteral("X-Enabled"), false));
}

void TestPluginMetadataHelpers::boolFalsyFallsBack()
{
    const KPluginMetaData md = metaFor({});
    QVERIFY(!metaBool(md, QStringLiteral("X-Missing"), false));
    QVERIFY(metaBool(md, QStringLiteral("X-Missing"), true));
}

void TestPluginMetadataHelpers::intValidRoundTrips()
{
    const KPluginMetaData md = metaFor({{QStringLiteral("X-Order"), QStringLiteral("42")}});
    QCOMPARE(metaInt(md, QStringLiteral("X-Order"), 0), 42);
}

void TestPluginMetadataHelpers::intInvalidFallsBack()
{
    const KPluginMetaData md = metaFor({{QStringLiteral("X-Order"), QStringLiteral("not-an-int")}});
    QCOMPARE(metaInt(md, QStringLiteral("X-Order"), 7), 7);
}

void TestPluginMetadataHelpers::stringListArrayReadsAll()
{
    QJsonArray arr{QStringLiteral("a"), QStringLiteral("b")};
    const KPluginMetaData md = metaFor({{QStringLiteral("X-Many"), arr}});
    QCOMPARE(metaStringList(md, QStringLiteral("X-Many")),
             (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
}

void TestPluginMetadataHelpers::stringListSingleStringWrapsInList()
{
    const KPluginMetaData md = metaFor({{QStringLiteral("X-One"), QStringLiteral("solo")}});
    QCOMPARE(metaStringList(md, QStringLiteral("X-One")),
             (QStringList{QStringLiteral("solo")}));
}

void TestPluginMetadataHelpers::stringListMissingReturnsEmpty()
{
    const KPluginMetaData md = metaFor({});
    QCOMPARE(metaStringList(md, QStringLiteral("X-Nope")), QStringList());
}

QTEST_MAIN(TestPluginMetadataHelpers)
#include "tst_pluginmetadatahelpers.moc"
