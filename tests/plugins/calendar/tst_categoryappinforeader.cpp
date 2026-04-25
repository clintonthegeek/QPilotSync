#include <QtTest/QtTest>

#include <cstring>

#include <pi-appinfo.h>

#include "plugins/calendar/categoryappinforeader.h"
#include "palm/calendar/categorymappingstore.h"

using WildPalms::CalendarPlugin::CategoryNames;
using WildPalms::CalendarPlugin::parseDatebookAppInfo;
using WildPalms::CalendarPlugin::populateFromAppInfo;
using WildPalms::PalmCalendar::CategoryMappingStore;

namespace {

// Build a minimum-valid AppInfo block with the named slots populated.
// Names must fit in pi-appinfo's 16-byte name field (15 chars + NUL).
//
// pisock on this system exposes only CategoryAppInfo_t / pack_CategoryAppInfo
// (no AppInfo_t / pack_AppInfo wrapper); the field layout matches the plan
// but is at the top level of the struct.
QByteArray buildAppInfoBytes(const QStringList &slotNames)
{
    CategoryAppInfo_t info;
    std::memset(&info, 0, sizeof(info));
    const int n = static_cast<int>(std::min<qsizetype>(slotNames.size(), 16));
    for (int i = 0; i < n; ++i) {
        const QByteArray utf = slotNames[i].toUtf8().left(15);
        std::memcpy(info.name[i], utf.constData(), utf.size());
        info.name[i][utf.size()] = '\0';
        info.ID[i] = static_cast<unsigned char>(i);
    }
    info.lastUniqueID = 15;

    QByteArray buf(4096, '\0');
    const int written = pack_CategoryAppInfo(
        &info,
        reinterpret_cast<unsigned char *>(buf.data()),
        buf.size());
    if (written < 0) return {};
    buf.resize(written);
    return buf;
}

} // namespace

class TestCategoryAppInfoReader : public QObject
{
    Q_OBJECT
private slots:
    void parseEmptyReturnsUnfiledOnly();
    void parsePopulatedReturnsNames();
    void parseTruncatedReturnsNullopt();
    void slotZeroForcedToUnfiledWhenBlank();
    void populatePopulatesNonEmptySlotsOnly();
    void populateFailureLeavesStoreUntouched();
};

void TestCategoryAppInfoReader::parseEmptyReturnsUnfiledOnly()
{
    QStringList none;
    QByteArray bytes = buildAppInfoBytes(none);
    QVERIFY(!bytes.isEmpty());

    auto result = parseDatebookAppInfo(bytes);
    QVERIFY(result.has_value());
    QCOMPARE(result->names[0], QStringLiteral("Unfiled"));
    for (int i = 1; i < 16; ++i) {
        QVERIFY2(result->names[i].isEmpty(),
            qPrintable(QStringLiteral("slot %1 expected empty").arg(i)));
    }
}

void TestCategoryAppInfoReader::parsePopulatedReturnsNames()
{
    QStringList names;
    names << QStringLiteral("Unfiled")     // 0
          << QStringLiteral("Work")        // 1
          << QStringLiteral("Personal");   // 2
    auto result = parseDatebookAppInfo(buildAppInfoBytes(names));
    QVERIFY(result.has_value());
    QCOMPARE(result->names[0], QStringLiteral("Unfiled"));
    QCOMPARE(result->names[1], QStringLiteral("Work"));
    QCOMPARE(result->names[2], QStringLiteral("Personal"));
    QVERIFY(result->names[3].isEmpty());
}

void TestCategoryAppInfoReader::parseTruncatedReturnsNullopt()
{
    auto result = parseDatebookAppInfo(QByteArray("\x00\x01", 2));
    QVERIFY(!result.has_value());
}

void TestCategoryAppInfoReader::slotZeroForcedToUnfiledWhenBlank()
{
    // Build with explicitly blank slot 0.
    QStringList names;
    names << QString()                     // 0 — blank, expect "Unfiled"
          << QStringLiteral("Work");
    auto result = parseDatebookAppInfo(buildAppInfoBytes(names));
    QVERIFY(result.has_value());
    QCOMPARE(result->names[0], QStringLiteral("Unfiled"));
    QCOMPARE(result->names[1], QStringLiteral("Work"));
}

void TestCategoryAppInfoReader::populatePopulatesNonEmptySlotsOnly()
{
    CategoryMappingStore store;
    QStringList names;
    names << QStringLiteral("Unfiled")
          << QStringLiteral("Work")
          << QString()                     // slot 2 blank — should NOT populate
          << QStringLiteral("Personal");
    QVERIFY(populateFromAppInfo(store, QStringLiteral("DatebookDB"),
                                buildAppInfoBytes(names)));

    QCOMPARE(store.slotName(QStringLiteral("DatebookDB"), 1),
             QStringLiteral("Work"));
    QVERIFY(store.slotName(QStringLiteral("DatebookDB"), 2).isEmpty());
    QCOMPARE(store.slotName(QStringLiteral("DatebookDB"), 3),
             QStringLiteral("Personal"));
    // populatedSlots returns sorted ascending list of named slots,
    // skipping slot 0.
    QList<int> populated = store.populatedSlots(QStringLiteral("DatebookDB"));
    QCOMPARE(populated, (QList<int>{1, 3}));
}

void TestCategoryAppInfoReader::populateFailureLeavesStoreUntouched()
{
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 1, QStringLiteral("Pre"));

    QVERIFY(!populateFromAppInfo(store, QStringLiteral("DatebookDB"),
                                 QByteArray("garbage", 7)));
    // Existing entry untouched.
    QCOMPARE(store.slotName(QStringLiteral("DatebookDB"), 1),
             QStringLiteral("Pre"));
}

QTEST_MAIN(TestCategoryAppInfoReader)
#include "tst_categoryappinforeader.moc"
