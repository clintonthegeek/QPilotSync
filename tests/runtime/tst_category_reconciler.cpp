// Substrate A3: pure category reconciliation over AppInfo bytes.
#include <QtTest/QtTest>
#include "../wildpalms_qtest_main.h"

#include "runtime/categoryreconciler.h"
#include "palm/categoryinfo.h"

using WildPalms::Runtime::reconcileCategories;

namespace {
// Build a synthetic AppInfo block: category region with the given names,
// followed by `tailBytes` of app-specific data that MUST survive untouched.
QByteArray makeAppInfoBlock(const QStringList &names, int tailBytes)
{
    CategoryInfo ci;
    // A zeroed table parses as empty; round-trip through pack to get valid
    // wire bytes. Slot 0 is conventionally "Unfiled".
    QByteArray zeroed(276, '\0');
    ci.parse(reinterpret_cast<const unsigned char *>(zeroed.constData()),
             static_cast<size_t>(zeroed.size()));
    ci.setCategory(0, QStringLiteral("Unfiled"));
    int slot = 1;
    for (const QString &n : names) ci.setCategory(slot++, n);
    QByteArray block(static_cast<int>(ci.packSize()) + tailBytes, '\0');
    ci.pack(reinterpret_cast<unsigned char *>(block.data()), ci.packSize());
    for (int i = static_cast<int>(ci.packSize()); i < block.size(); ++i)
        block[i] = static_cast<char>(0xAB);   // sentinel tail
    return block;
}
} // namespace

class TstCategoryReconciler : public QObject {
    Q_OBJECT
private slots:
    void existingNamesBindWithoutWrite();
    void missingNamesClaimSlotsAndRewrite();
    void caseInsensitiveMatch();
    void fullTableReportsNoFreeSlot();
    void appSpecificTailPreserved();
};

void TstCategoryReconciler::existingNamesBindWithoutWrite()
{
    const auto block = makeAppInfoBlock({ QStringLiteral("Work") }, 4);
    const auto r = reconcileCategories(block, { QStringLiteral("Work") });
    QCOMPARE(r.bound.value(QStringLiteral("Work")), 1);
    QVERIFY(r.updatedAppInfoBlock.isEmpty());     // nothing to write
    QVERIFY(r.noFreeSlot.isEmpty());
}

void TstCategoryReconciler::missingNamesClaimSlotsAndRewrite()
{
    const auto block = makeAppInfoBlock({ QStringLiteral("Work") }, 4);
    const auto r = reconcileCategories(block,
        { QStringLiteral("Work"), QStringLiteral("Errands") });
    QCOMPARE(r.bound.value(QStringLiteral("Work")), 1);
    QCOMPARE(r.bound.value(QStringLiteral("Errands")), 2);
    QVERIFY(!r.updatedAppInfoBlock.isEmpty());
    // Round-trip: the written block parses and contains the new name.
    CategoryInfo check;
    QVERIFY(check.parse(
        reinterpret_cast<const unsigned char *>(r.updatedAppInfoBlock.constData()),
        static_cast<size_t>(r.updatedAppInfoBlock.size())));
    QCOMPARE(check.categoryName(2), QStringLiteral("Errands"));
}

void TstCategoryReconciler::caseInsensitiveMatch()
{
    const auto block = makeAppInfoBlock({ QStringLiteral("work") }, 0);
    const auto r = reconcileCategories(block, { QStringLiteral("Work") });
    QCOMPARE(r.bound.value(QStringLiteral("Work")), 1);
    QVERIFY(r.updatedAppInfoBlock.isEmpty());
}

void TstCategoryReconciler::fullTableReportsNoFreeSlot()
{
    QStringList fifteen;
    for (int i = 1; i <= 15; ++i) fifteen << QStringLiteral("Cat%1").arg(i);
    const auto block = makeAppInfoBlock(fifteen, 0);
    const auto r = reconcileCategories(block, { QStringLiteral("Overflow") });
    QVERIFY(r.bound.isEmpty() || !r.bound.contains(QStringLiteral("Overflow")));
    QCOMPARE(r.noFreeSlot, QStringList{ QStringLiteral("Overflow") });
    QVERIFY(r.updatedAppInfoBlock.isEmpty());
}

void TstCategoryReconciler::appSpecificTailPreserved()
{
    const auto block = makeAppInfoBlock({}, 8);   // 8 sentinel bytes after categories
    const auto r = reconcileCategories(block, { QStringLiteral("New") });
    QVERIFY(!r.updatedAppInfoBlock.isEmpty());
    QCOMPARE(r.updatedAppInfoBlock.size(), block.size());
    QCOMPARE(r.updatedAppInfoBlock.right(8), block.right(8));   // tail untouched
}

WILDPALMS_QTEST_MAIN(TstCategoryReconciler)
#include "tst_category_reconciler.moc"
