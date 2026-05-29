// K.8b T6: Rewritten to use static plugins directly.
//
// Before T1-T5 the five Palm backend plugins were KCoreAddons MODULE
// plugins discovered via KPluginFactory/KPluginMetaData.  After T1-T5
// they are compiled STATIC into WildPalmsCore.  This test now directly
// instantiates each plugin and verifies the hasMainView()/createMainView()
// / mainViewName() contract that KF6MainWindow depends on — the same
// assertions as before, just without KPluginFactory indirection.
//
// See FINDINGS.md for the V1-vs-V2 plumbing gap discovered during M5c.

#include <QtTest/QtTest>
#include <QApplication>
#include <QWidget>

#include "plugins/calendar/calendarbackendplugin.h"
#include "plugins/contacts/contactsbackendplugin.h"
#include "plugins/memo/memobackendplugin.h"
#include "plugins/todos/todobackendplugin.h"

class TstMainWindowPluginPagesPopulated : public QObject
{
    Q_OBJECT
private slots:
    void v2_plugins_with_main_view_create_non_null_widgets();
};

// A thin wrapper so we can iterate over the five plugins uniformly.
struct PluginEntry {
    QString         id;
    bool            hasView;
    std::function<QWidget*(QWidget*)> createView;
    std::function<QString()>          viewName;
};

void TstMainWindowPluginPagesPopulated::v2_plugins_with_main_view_create_non_null_widgets()
{
    // Instantiate the four static Palm backend plugins.
    WildPalms::CalendarPlugin::CalendarBackendPlugin  cal;
    WildPalms::ContactsPlugin::ContactsBackendPlugin  con;
    WildPalms::Memo::MemoPlugin                       memo;
    WildPalms::TodoPlugin::TodoBackendPlugin           todo;

    QWidget parent;

    // Build a uniform list to iterate.
    QList<PluginEntry> entries = {
        { QStringLiteral("calendar"),    cal.hasMainView(),
          [&](QWidget *p){ return cal.createMainView(p); },
          [&](){ return cal.mainViewName(); } },
        { QStringLiteral("contacts"),    con.hasMainView(),
          [&](QWidget *p){ return con.createMainView(p); },
          [&](){ return con.mainViewName(); } },
        { QStringLiteral("memo"),        memo.hasMainView(),
          [&](QWidget *p){ return memo.createMainView(p); },
          [&](){ return memo.mainViewName(); } },
        { QStringLiteral("todo"),        todo.hasMainView(),
          [&](QWidget *p){ return todo.createMainView(p); },
          [&](){ return todo.mainViewName(); } },
    };

    QStringList viewPluginIds;
    QStringList nonViewPluginIds;

    for (auto &e : entries) {
        if (e.hasView) {
            QVERIFY2(e.createView != nullptr,
                     qPrintable(QStringLiteral("Plugin '%1' claims hasMainView() but "
                                               "createView functor is null").arg(e.id)));
            QWidget *view = e.createView(&parent);
            QVERIFY2(view != nullptr,
                qPrintable(QStringLiteral(
                    "Plugin '%1' claims hasMainView()=true but "
                    "createMainView() returned nullptr — any per-plugin "
                    "KPageWidget wiring would silently skip it.")
                    .arg(e.id)));
            QVERIFY2(!e.viewName().isEmpty(),
                qPrintable(QStringLiteral(
                    "Plugin '%1' claims hasMainView()=true but "
                    "mainViewName() is empty — page header would be blank.")
                    .arg(e.id)));
            viewPluginIds << e.id;
        } else {
            nonViewPluginIds << e.id;
        }
    }

    QVERIFY2(!viewPluginIds.isEmpty(),
             "Expected at least one Palm plugin with hasMainView()=true; "
             "any per-plugin page area would be empty.");

    qInfo() << "Palm plugins with main view:" << viewPluginIds;
    qInfo() << "Palm plugins without main view:" << nonViewPluginIds;
}

QTEST_MAIN(TstMainWindowPluginPagesPopulated)
#include "tst_main_window_plugin_pages_populated.moc"
