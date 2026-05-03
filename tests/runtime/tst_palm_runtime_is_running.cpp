#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include "runtime/palmruntime.h"
#include "runtime/palmrunresult.h"

class TstPalmRuntimeIsRunning : public QObject
{
    Q_OBJECT
private slots:
    void starts_false();
    void true_after_runStarted();
    void false_after_runFinished();
};

void TstPalmRuntimeIsRunning::starts_false()
{
    QTemporaryDir tmp;
    WildPalms::Runtime::PalmRuntime rt(tmp.path());
    QCOMPARE(rt.isRunning(), false);
}

void TstPalmRuntimeIsRunning::true_after_runStarted()
{
    QTemporaryDir tmp;
    WildPalms::Runtime::PalmRuntime rt(tmp.path());
    emit rt.runStarted(QStringLiteral("HotSync"));
    QCOMPARE(rt.isRunning(), true);
}

void TstPalmRuntimeIsRunning::false_after_runFinished()
{
    QTemporaryDir tmp;
    WildPalms::Runtime::PalmRuntime rt(tmp.path());
    emit rt.runStarted(QStringLiteral("HotSync"));
    WildPalms::Runtime::PalmRunResult result;
    emit rt.runFinished(result);
    QCOMPARE(rt.isRunning(), false);
}

QTEST_MAIN(TstPalmRuntimeIsRunning)
#include "tst_palm_runtime_is_running.moc"
