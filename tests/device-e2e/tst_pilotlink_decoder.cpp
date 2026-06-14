#include <QTest>
#include "pilotlinkdecoder.h"
#include "../wildpalms_qtest_main.h"

extern "C" {
#include <pi-buffer.h>
#include <pi-datebook.h>
}

using namespace WildPalms::DeviceE2E;

class TestPilotLinkDecoder : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qputenv("TZ", "UTC");
        tzset();
    }

    void decodesTimedAppointmentWithAlarmAndNote()
    {
        // Build a known appointment with pilot-link's own packer.
        Appointment_t a{};
        a.event = 0; // timed
        a.begin = tm{}; a.begin.tm_year = 126; a.begin.tm_mon = 6; a.begin.tm_mday = 1;
        a.begin.tm_hour = 9;  a.begin.tm_min = 0; a.begin.tm_sec = 0;
        a.end = tm{};   a.end.tm_year = 126;   a.end.tm_mon = 6;   a.end.tm_mday = 1;
        a.end.tm_hour = 10;   a.end.tm_min = 0;   a.end.tm_sec = 0;
        a.alarm = 1; a.advance = 10; a.advanceUnits = advMinutes;
        a.repeatType = repeatNone; a.repeatForever = 0; a.exceptions = 0; a.exception = nullptr;
        char desc[] = "Seeded Event";
        char note[] = "Note body text";
        a.description = desc;
        a.note = note;

        pi_buffer_t *buf = pi_buffer_new(256);
        QVERIFY(buf);
        const int packed = pack_Appointment(&a, buf, datebook_v1);
        QVERIFY(packed >= 0);
        const QByteArray raw(reinterpret_cast<const char *>(buf->data), int(buf->used));
        pi_buffer_free(buf);

        bool ok = false;
        const DecodedAppointment d = decodeAppointmentRecord(raw, /*category=*/0, &ok);
        QVERIFY(ok);
        QCOMPARE(d.description, QStringLiteral("Seeded Event"));
        QCOMPARE(d.note, QStringLiteral("Note body text"));
        QCOMPARE(d.allDay, false);
        QCOMPARE(d.begin, QDateTime(QDate(2026, 7, 1), QTime(9, 0, 0)));
        QCOMPARE(d.end, QDateTime(QDate(2026, 7, 1), QTime(10, 0, 0)));
        QCOMPARE(d.hasAlarm, true);
        QCOMPARE(d.advance, 10);
        QCOMPARE(d.advanceUnits, int(advMinutes));
        QCOMPARE(d.category, 0);
    }
};

WILDPALMS_QTEST_GUILESS_MAIN(TestPilotLinkDecoder)
#include "tst_pilotlink_decoder.moc"
