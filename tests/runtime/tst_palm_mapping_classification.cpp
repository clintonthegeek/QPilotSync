#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QJsonArray>
#include "runtime/palmruntime.h"

using namespace WildPalms::Runtime;

class TstPalmMappingClassification : public QObject { Q_OBJECT
private slots:
    void palm_direct_mapping_recognized()
    {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        PalmRuntime runtime(tmp.path());

        QJsonObject m;
        m["id"]              = "test-palm-cal";
        m["sourceBackend"]   = "wp-hub";
        m["sourceCalendar"]  = "palm:calendar";
        m["targetBackend"]   = "calendar";          // a Palm-side plugin id
        m["targetCalendar"]  = "palm:calendar/0";
        m["mode"]            = "TwoWay";
        m["enabled"]         = true;
        QJsonArray arr; arr.append(m);
        runtime.reloadMappings(arr);

        const auto ids = runtime.palmDirectMappingsForDomain(QStringLiteral("calendar"));
        QCOMPARE(ids.size(), 1);
        QCOMPARE(ids.first(), QStringLiteral("test-palm-cal"));
    }

    void route_mapping_excluded()
    {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        PalmRuntime runtime(tmp.path());

        QJsonObject m;
        m["id"]              = "test-route";
        m["sourceBackend"]   = "wp-hub";
        m["sourceCalendar"]  = "palm:calendar";
        m["targetBackend"]   = "caldav-personal";   // a remote backend, not Palm
        m["targetCalendar"]  = "personal";
        m["mode"]            = "TwoWay";
        m["enabled"]         = true;
        QJsonArray arr; arr.append(m);
        runtime.reloadMappings(arr);

        QCOMPARE(runtime.palmDirectMappingsForDomain(QStringLiteral("calendar")).size(), 0);
    }
};
QTEST_GUILESS_MAIN(TstPalmMappingClassification)
#include "tst_palm_mapping_classification.moc"
