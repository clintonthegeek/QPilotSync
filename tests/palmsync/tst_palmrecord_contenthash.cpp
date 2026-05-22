#include <QtTest/QtTest>

#include "palm/sync/palmrecord.h"

using WildPalms::PalmSync::PalmRecord;

class TstPalmRecordContentHash : public QObject
{
    Q_OBJECT
private slots:
    void hashStableAcrossAttributeChanges();
    void hashStableAcrossLastModifiedChanges();
    void hashChangesWhenDataChanges();
    void hashChangesWhenCategoryChanges();
    void hashChangesWhenRecordIdChanges();
    void hashEmptyRecordIsDeterministic();
};

void TstPalmRecordContentHash::hashStableAcrossAttributeChanges()
{
    PalmRecord a;
    a.recordId = 42;
    a.category = 3;
    a.data     = QByteArrayLiteral("hello");
    a.attributes = 0x00;
    a.lastModified = QDateTime::fromString(QStringLiteral("2026-05-22T10:00:00Z"), Qt::ISODate);

    const QString h0 = a.contentHash();

    a.attributes = PalmRecord::AttrDirty;
    QCOMPARE(a.contentHash(), h0);

    a.attributes = PalmRecord::AttrDirty | PalmRecord::AttrBusy;
    QCOMPARE(a.contentHash(), h0);

    a.attributes = PalmRecord::AttrSecret | PalmRecord::AttrArchived;
    QCOMPARE(a.contentHash(), h0);
}

void TstPalmRecordContentHash::hashStableAcrossLastModifiedChanges()
{
    PalmRecord a;
    a.recordId = 42;
    a.category = 3;
    a.data     = QByteArrayLiteral("hello");
    a.lastModified = QDateTime::fromString(QStringLiteral("2026-05-22T10:00:00Z"), Qt::ISODate);

    const QString h0 = a.contentHash();

    a.lastModified = QDateTime::fromString(QStringLiteral("2026-05-23T10:00:00Z"), Qt::ISODate);
    QCOMPARE(a.contentHash(), h0);

    a.lastModified = QDateTime();
    QCOMPARE(a.contentHash(), h0);
}

void TstPalmRecordContentHash::hashChangesWhenDataChanges()
{
    PalmRecord a;
    a.recordId = 42;
    a.category = 3;
    a.data     = QByteArrayLiteral("hello");
    const QString h0 = a.contentHash();

    a.data = QByteArrayLiteral("hello!");
    QVERIFY(a.contentHash() != h0);
}

void TstPalmRecordContentHash::hashChangesWhenCategoryChanges()
{
    PalmRecord a;
    a.recordId = 42;
    a.category = 3;
    a.data     = QByteArrayLiteral("hello");
    const QString h0 = a.contentHash();

    a.category = 4;
    QVERIFY(a.contentHash() != h0);
}

void TstPalmRecordContentHash::hashChangesWhenRecordIdChanges()
{
    PalmRecord a;
    a.recordId = 42;
    a.category = 3;
    a.data     = QByteArrayLiteral("hello");
    const QString h0 = a.contentHash();

    a.recordId = 43;
    QVERIFY(a.contentHash() != h0);
}

void TstPalmRecordContentHash::hashEmptyRecordIsDeterministic()
{
    PalmRecord a, b;
    QCOMPARE(a.contentHash(), b.contentHash());
    QVERIFY(!a.contentHash().isEmpty());
}

QTEST_GUILESS_MAIN(TstPalmRecordContentHash)
#include "tst_palmrecord_contenthash.moc"
