#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "../wildpalms_qtest_main.h"
#include "runtime/profileregistry.h"

#include <KSharedConfig>

using WildPalms::Runtime::ProfileRegistry;
using WildPalms::Runtime::ProfileEntry;

class TstProfileRegistrySerialBinding : public QObject {
    Q_OBJECT
private slots:
    void serialFieldDefaultsEmpty();
    void bindSerialPersists();
    void findBySerialReturnsBoundEntry();
    void bindSerialOnNewProfileWithoutSerial();
    void unregisterCascadesSerialBinding();
    void bindSerialMovesAcrossEntries();
};

namespace {
std::unique_ptr<ProfileRegistry> makeRegistry(QTemporaryDir &dir) {
    auto cfg = KSharedConfig::openConfig(
        dir.path() + QStringLiteral("/wprc"));
    auto r = std::make_unique<ProfileRegistry>(cfg);
    r->setDefaultRoot(dir.path() + QStringLiteral("/root"));
    return r;
}
} // namespace

void TstProfileRegistrySerialBinding::serialFieldDefaultsEmpty()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto reg = makeRegistry(d);
    const auto e = reg->registerNew(QStringLiteral("A"));
    QVERIFY(e.isValid());
    QCOMPARE(e.usbSerial, QString());
}

void TstProfileRegistrySerialBinding::bindSerialPersists()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto cfgPath = d.path() + QStringLiteral("/wprc");
    {
        auto cfg = KSharedConfig::openConfig(cfgPath);
        ProfileRegistry reg(cfg);
        reg.setDefaultRoot(d.path() + QStringLiteral("/root"));
        const auto e = reg.registerNew(QStringLiteral("Palm m505"));
        QVERIFY(reg.bindSerial(e.id, QStringLiteral("L0JG14I11398")));
    }
    // Reopen and verify persistence.
    auto cfg = KSharedConfig::openConfig(cfgPath);
    ProfileRegistry reg(cfg);
    const auto entries = reg.entries();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().usbSerial, QStringLiteral("L0JG14I11398"));
}

void TstProfileRegistrySerialBinding::findBySerialReturnsBoundEntry()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto reg = makeRegistry(d);
    const auto e = reg->registerNew(QStringLiteral("Palm"));
    QVERIFY(reg->bindSerial(e.id, QStringLiteral("SN-1")));
    const auto found = reg->findBySerial(QStringLiteral("SN-1"));
    QVERIFY(found.isValid());
    QCOMPARE(found.id, e.id);
}

void TstProfileRegistrySerialBinding::bindSerialOnNewProfileWithoutSerial()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto reg = makeRegistry(d);
    QVERIFY(!reg->findBySerial(QStringLiteral("missing")).isValid());
    QVERIFY(!reg->findBySerial(QString()).isValid());
}

void TstProfileRegistrySerialBinding::unregisterCascadesSerialBinding()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto reg = makeRegistry(d);
    const auto e = reg->registerNew(QStringLiteral("A"));
    QVERIFY(reg->bindSerial(e.id, QStringLiteral("SN-A")));
    QVERIFY(reg->unregister(e.id));
    QVERIFY(!reg->findBySerial(QStringLiteral("SN-A")).isValid());
}

void TstProfileRegistrySerialBinding::bindSerialMovesAcrossEntries()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto reg = makeRegistry(d);
    const auto a = reg->registerNew(QStringLiteral("A"));
    const auto b = reg->registerNew(QStringLiteral("B"));
    QVERIFY(reg->bindSerial(a.id, QStringLiteral("SN-X")));
    QCOMPARE(reg->findBySerial(QStringLiteral("SN-X")).id, a.id);

    // Bind same serial to b: should move off a.
    QVERIFY(reg->bindSerial(b.id, QStringLiteral("SN-X")));
    QCOMPARE(reg->findBySerial(QStringLiteral("SN-X")).id, b.id);
    QCOMPARE(reg->entry(a.id).usbSerial, QString());
}

WILDPALMS_QTEST_GUILESS_MAIN(TstProfileRegistrySerialBinding)
#include "tst_profileregistry_serial_binding.moc"
