// tests/runtime/tst_profilepropertiesdialog_rename.cpp
#include <QtTest/QtTest>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "../../src/widgets/dialogs/profilepropertiesdialog.h"
#include "../../src/profile.h"
#include "../wildpalms_qtest_main.h"

class TstProfilePropertiesDialogRename : public QObject
{
    Q_OBJECT
private slots:
    void renameEmitsSignal();
    void noChangeNoSignal();
    void whitespaceOnlyIgnored();
    void nameFieldPrefilledFromProfile();
    void dialogDoesNotMutateProfileName();

private:
    QLineEdit *findNameEdit(ProfilePropertiesDialog *dlg);
    std::unique_ptr<Profile> makeProfile(const QString &name,
                                          const QString &path);
};

QLineEdit *TstProfilePropertiesDialogRename::findNameEdit(
    ProfilePropertiesDialog *dlg)
{
    auto edits = dlg->findChildren<QLineEdit *>(
        QStringLiteral("profileName"));
    return edits.isEmpty() ? nullptr : edits.first();
}

std::unique_ptr<Profile> TstProfilePropertiesDialogRename::makeProfile(
    const QString &name, const QString &path)
{
    auto p = std::make_unique<Profile>();
    p->setName(name);
    p->setSyncFolderPath(path);
    return p;
}

void TstProfilePropertiesDialogRename::nameFieldPrefilledFromProfile()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto profile = makeProfile(QStringLiteral("Original"), tmp.path());
    ProfilePropertiesDialog dlg(profile.get());

    auto *edit = findNameEdit(&dlg);
    QVERIFY(edit);
    QCOMPARE(edit->text(), QStringLiteral("Original"));
}

void TstProfilePropertiesDialogRename::renameEmitsSignal()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto profile = makeProfile(QStringLiteral("Original"), tmp.path());
    ProfilePropertiesDialog dlg(profile.get());
    auto *edit = findNameEdit(&dlg);
    QVERIFY(edit);
    edit->setText(QStringLiteral("Renamed"));

    QSignalSpy spy(&dlg, &ProfilePropertiesDialog::renameRequested);
    QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(1).toString(), QStringLiteral("Renamed"));
}

void TstProfilePropertiesDialogRename::noChangeNoSignal()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto profile = makeProfile(QStringLiteral("Original"), tmp.path());
    ProfilePropertiesDialog dlg(profile.get());

    QSignalSpy spy(&dlg, &ProfilePropertiesDialog::renameRequested);
    QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection);
    QCOMPARE(spy.count(), 0);
}

void TstProfilePropertiesDialogRename::whitespaceOnlyIgnored()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto profile = makeProfile(QStringLiteral("Original"), tmp.path());
    ProfilePropertiesDialog dlg(profile.get());
    auto *edit = findNameEdit(&dlg);
    QVERIFY(edit);
    edit->setText(QStringLiteral("   "));

    QSignalSpy spy(&dlg, &ProfilePropertiesDialog::renameRequested);
    QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection);
    QCOMPARE(spy.count(), 0);
}

void TstProfilePropertiesDialogRename::dialogDoesNotMutateProfileName()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto profile = makeProfile(QStringLiteral("Original"), tmp.path());
    ProfilePropertiesDialog dlg(profile.get());
    auto *edit = findNameEdit(&dlg);
    QVERIFY(edit);
    edit->setText(QStringLiteral("Renamed"));

    QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection);
    QCOMPARE(profile->name(), QStringLiteral("Original"));
}

WILDPALMS_QTEST_MAIN(TstProfilePropertiesDialogRename)
#include "tst_profilepropertiesdialog_rename.moc"
