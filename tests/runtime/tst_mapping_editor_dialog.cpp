#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonObject>

#include "mappingeditordialog.h"

class TstMappingEditorDialog : public QObject
{
    Q_OBJECT
private slots:
    void round_trips_json_array();
    void delete_row_removes_from_output();
};

static QJsonObject makeMapping(const QString &id, const QString &source)
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("sourceBackend")] = QStringLiteral("calendar-palm");
    o[QStringLiteral("sourceCalendar")] = source;
    o[QStringLiteral("targetBackend")] = QStringLiteral("rawfiles-cal");
    o[QStringLiteral("targetCalendar")] = source + QStringLiteral("-out");
    o[QStringLiteral("mode")] = QStringLiteral("two-way");
    o[QStringLiteral("conflictResolution")] = QStringLiteral("ask-user");
    o[QStringLiteral("lossPolicy")] = QStringLiteral("warn");
    o[QStringLiteral("enabled")] = true;
    return o;
}

void TstMappingEditorDialog::round_trips_json_array()
{
    QJsonArray in;
    in.append(makeMapping(QStringLiteral("a"), QStringLiteral("cal-A")));
    in.append(makeMapping(QStringLiteral("b"), QStringLiteral("cal-B")));

    MappingEditorDialog dlg;
    dlg.setMappings(in);

    auto out = dlg.mappings();
    QCOMPARE(out.size(), 2);
    QCOMPARE(out.at(0).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("a"));
    QCOMPARE(out.at(1).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("b"));
}

void TstMappingEditorDialog::delete_row_removes_from_output()
{
    QJsonArray in;
    in.append(makeMapping(QStringLiteral("a"), QStringLiteral("cal-A")));
    in.append(makeMapping(QStringLiteral("b"), QStringLiteral("cal-B")));

    MappingEditorDialog dlg;
    dlg.setMappings(in);
    dlg.removeRowForTest(0);

    auto out = dlg.mappings();
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.at(0).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("b"));
}

QTEST_MAIN(TstMappingEditorDialog)
#include "tst_mapping_editor_dialog.moc"
