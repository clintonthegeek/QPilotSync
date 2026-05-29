// Sub-project D Task 5: Contacts joins the V2 sidebar.
//
// Pins the hasMainView() contract — the V2 sidebar iterates plugins
// and calls createMainView() for those that return true here.

#include <QtTest/QtTest>

#include "plugins/contacts/contactsbackendplugin.h"

class TstContactsInSidebar : public QObject
{
    Q_OBJECT
private slots:
    void hasMainViewIsTrue();
};

void TstContactsInSidebar::hasMainViewIsTrue()
{
    WildPalms::ContactsPlugin::ContactsBackendPlugin plugin;
    QVERIFY(plugin.hasMainView());
}

QTEST_MAIN(TstContactsInSidebar)
#include "tst_contacts_in_sidebar.moc"
