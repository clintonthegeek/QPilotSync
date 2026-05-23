// tests/runtime/tst_profile_category_snapshot.cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "../../src/profile.h"
#include "../wildpalms_qtest_main.h"

class TstProfileCategorySnapshot : public QObject {
    Q_OBJECT
private slots:
    void emptyByDefault();
    void roundTripsSnapshot();
    void slot0ForcedToUnfiled();
    void overwriteReplacesSlots();
    void differentDbsAreIndependent();
};

namespace {
QStringList sixteenEntries(std::initializer_list<std::pair<int, QString>> entries) {
    QStringList out;
    out.reserve(16);
    for (int i = 0; i < 16; ++i) out << QString();
    for (const auto &[slot, name] : entries) {
        Q_ASSERT(slot >= 0 && slot < 16);
        out[slot] = name;
    }
    return out;
}
} // namespace

void TstProfileCategorySnapshot::emptyByDefault()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Profile profile(dir.path()); profile.initialize();

    const auto names = profile.categorySlotNames(QStringLiteral("DatebookDB"));
    QVERIFY(names.isEmpty());
}

void TstProfileCategorySnapshot::roundTripsSnapshot()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    {
        Profile profile(dir.path()); profile.initialize();
        const auto names = sixteenEntries({
            {0, QStringLiteral("Unfiled")},
            {1, QStringLiteral("Work")},
            {2, QStringLiteral("Personal")},
        });
        profile.setCategorySlotNames(QStringLiteral("DatebookDB"), names);
    }

    // Reopen — same dir.
    Profile reopened(dir.path()); reopened.load();
    const auto names = reopened.categorySlotNames(QStringLiteral("DatebookDB"));
    QCOMPARE(names.size(), 16);
    QCOMPARE(names.at(0), QStringLiteral("Unfiled"));
    QCOMPARE(names.at(1), QStringLiteral("Work"));
    QCOMPARE(names.at(2), QStringLiteral("Personal"));
    QCOMPARE(names.at(3), QString());
}

void TstProfileCategorySnapshot::slot0ForcedToUnfiled()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Profile profile(dir.path()); profile.initialize();

    // Pass empty slot 0 → API forces "Unfiled" on read.
    auto names = sixteenEntries({{1, QStringLiteral("Work")}});
    names[0] = QString();
    profile.setCategorySlotNames(QStringLiteral("DatebookDB"), names);

    const auto out = profile.categorySlotNames(QStringLiteral("DatebookDB"));
    QCOMPARE(out.at(0), QStringLiteral("Unfiled"));
    QCOMPARE(out.at(1), QStringLiteral("Work"));
}

void TstProfileCategorySnapshot::overwriteReplacesSlots()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Profile profile(dir.path()); profile.initialize();

    profile.setCategorySlotNames(QStringLiteral("DatebookDB"),
        sixteenEntries({{1, QStringLiteral("Old1")}, {2, QStringLiteral("Old2")}}));

    profile.setCategorySlotNames(QStringLiteral("DatebookDB"),
        sixteenEntries({{1, QStringLiteral("New1")}}));

    const auto out = profile.categorySlotNames(QStringLiteral("DatebookDB"));
    QCOMPARE(out.at(1), QStringLiteral("New1"));
    QCOMPARE(out.at(2), QString());   // old value cleared
}

void TstProfileCategorySnapshot::differentDbsAreIndependent()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Profile profile(dir.path()); profile.initialize();

    profile.setCategorySlotNames(QStringLiteral("DatebookDB"),
        sixteenEntries({{1, QStringLiteral("CalWork")}}));
    profile.setCategorySlotNames(QStringLiteral("AddressDB"),
        sixteenEntries({{1, QStringLiteral("AddrWork")}}));

    QCOMPARE(profile.categorySlotNames(QStringLiteral("DatebookDB")).at(1),
             QStringLiteral("CalWork"));
    QCOMPARE(profile.categorySlotNames(QStringLiteral("AddressDB")).at(1),
             QStringLiteral("AddrWork"));
}

WILDPALMS_QTEST_GUILESS_MAIN(TstProfileCategorySnapshot)
#include "tst_profile_category_snapshot.moc"
