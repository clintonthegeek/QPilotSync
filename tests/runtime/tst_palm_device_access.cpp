#include <QTest>
#include <QThread>
#include <atomic>

#include "runtime/palmdeviceaccess.h"
#include "palm/sync/ipalmdatabaseaccess.h"
#include "palm/sync/palmrecord.h"

using namespace WildPalms::Runtime;
using namespace WildPalms::PalmSync;

namespace {

class ThreadCapturingMock : public IPalmDatabaseAccess {
public:
    QStringList availableDatabases() const override {
        m_lastCallThread.store(QThread::currentThreadId());
        return { QStringLiteral("MemoDB") };
    }
    bool hasDatabase(const QString &) const override {
        m_lastCallThread.store(QThread::currentThreadId());
        return true;
    }
    bool createDatabase(const QString &) override {
        m_lastCallThread.store(QThread::currentThreadId());
        return true;
    }
    QList<PalmRecord> readAllRecords(const QString &) const override {
        m_lastCallThread.store(QThread::currentThreadId());
        return {};
    }
    std::optional<PalmRecord> readRecord(const QString &, std::uint32_t) const override {
        m_lastCallThread.store(QThread::currentThreadId());
        return std::nullopt;
    }
    std::uint32_t createRecord(const QString &, const PalmRecord &) override {
        m_lastCallThread.store(QThread::currentThreadId());
        return 1;
    }
    bool updateRecord(const QString &, const PalmRecord &) override {
        m_lastCallThread.store(QThread::currentThreadId());
        return true;
    }
    bool deleteRecord(const QString &, std::uint32_t) override {
        m_lastCallThread.store(QThread::currentThreadId());
        return true;
    }
    QList<PalmRecord> recordsModifiedSince(const QString &, const QDateTime &) const override {
        m_lastCallThread.store(QThread::currentThreadId());
        return {};
    }
    QList<std::uint32_t> recordsDeletedSince(const QString &, const QDateTime &) const override {
        m_lastCallThread.store(QThread::currentThreadId());
        return {};
    }
    QByteArray readAppBlock(const QString &) const override {
        m_lastCallThread.store(QThread::currentThreadId());
        return {};
    }
    bool writeAppBlock(const QString &, const QByteArray &) override {
        m_lastCallThread.store(QThread::currentThreadId());
        return true;
    }
    bool supportsDeleteTracking() const override {
        m_lastCallThread.store(QThread::currentThreadId());
        return false;
    }
    bool isConnected() const override { return true; }

    Qt::HANDLE lastCallThread() const { return m_lastCallThread.load(); }

private:
    mutable std::atomic<Qt::HANDLE> m_lastCallThread{nullptr};
};

}  // namespace

class TestPalmDeviceAccess : public QObject {
    Q_OBJECT
private slots:
    void readAllRecords_dispatchedToLinkThread() {
        auto mock = std::make_unique<ThreadCapturingMock>();
        ThreadCapturingMock *mockRaw = mock.get();
        PalmDeviceAccess access(std::move(mock));

        const Qt::HANDLE callerTid = QThread::currentThreadId();

        // Sanity: the link thread must be a distinct thread object from the
        // test's main thread (QThread::currentThreadId() is static; comparing
        // QThread pointers is the reliable way to check "different thread").
        QVERIFY(access.linkThread() != QThread::currentThread());
        QVERIFY(access.linkThread()->isRunning());

        (void)access.readAllRecords(QStringLiteral("MemoDB"));

        const Qt::HANDLE seen = mockRaw->lastCallThread();
        QVERIFY2(seen != callerTid,
                 "readAllRecords ran on the caller's thread; marshalling failed");
    }

    void allMethodsMarshall() {
        auto mock = std::make_unique<ThreadCapturingMock>();
        ThreadCapturingMock *mockRaw = mock.get();
        PalmDeviceAccess access(std::move(mock));

        const Qt::HANDLE callerTid = QThread::currentThreadId();

        access.availableDatabases();             QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.hasDatabase(QStringLiteral("X")); QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.createDatabase(QStringLiteral("X")); QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.readAllRecords(QStringLiteral("X")); QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.readRecord(QStringLiteral("X"), 1);  QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.createRecord(QStringLiteral("X"), PalmRecord{}); QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.updateRecord(QStringLiteral("X"), PalmRecord{}); QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.deleteRecord(QStringLiteral("X"), 1);  QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.recordsModifiedSince(QStringLiteral("X"), {});  QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.recordsDeletedSince(QStringLiteral("X"), {});   QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.readAppBlock(QStringLiteral("X"));               QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.supportsDeleteTracking();                         QVERIFY(mockRaw->lastCallThread() != callerTid);
    }
};

QTEST_GUILESS_MAIN(TestPalmDeviceAccess)
#include "tst_palm_device_access.moc"
