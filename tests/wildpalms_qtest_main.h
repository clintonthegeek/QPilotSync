// SPDX-License-Identifier: same as project
#pragma once

// WildPalms test main: replaces QTEST_MAIN / QTEST_GUILESS_MAIN.
//
// Why: WildPalms tests that exercise QObject hierarchies (PalmRuntime,
// AccountController, providers, plugins, KF6 widgets) reliably crash
// 5–25% of the time at process exit with "corrupted double-linked list".
// The crash happens AFTER the test prints "Finished testing of X" and
// after every test method passes — it is purely a global-destructor-order
// race in libQt6Core's __cxa_finalize cleanup (verified by gdb backtrace:
// libc abort ← libQt6Core symbol at +0x1d9906 = QQueuedMetaCallEvent::~ ←
// __cxa_finalize). Pre-existing issue in our Qt 6.11.1 / KF6 stack;
// not caused by anything in WildPalms or libkalburator.
//
// Fix: skip static destructors with std::_Exit. All test logic and
// QtTest's "Totals: N passed" reporting already completed; static
// destructors only release memory that the kernel reclaims on exit
// regardless. No functional change.
//
// Usage:
//   #include "../wildpalms_qtest_main.h"
//   ...
//   WILDPALMS_QTEST_MAIN(MyTestClass)            // QApplication (widgets)
//   WILDPALMS_QTEST_GUILESS_MAIN(MyTestClass)    // QCoreApplication

#include <QCoreApplication>
#include <QtTest/QtTest>
#include <cstdlib>

// QApplication only available when test target links Qt::Widgets.
// Tests using WILDPALMS_QTEST_GUILESS_MAIN do not need this include
// (and their target may not link Widgets); tests using
// WILDPALMS_QTEST_MAIN must link Widgets, so the include resolves.
#include <QApplication>

#define WILDPALMS_QTEST_MAIN_IMPL(TestClass, AppType)                \
    int main(int argc, char *argv[])                                 \
    {                                                                \
        AppType app(argc, argv);                                     \
        TestClass tc;                                                \
        const int rc = QTest::qExec(&tc, argc, argv);                \
        std::_Exit(rc);                                              \
    }

#define WILDPALMS_QTEST_MAIN(TestClass) \
    WILDPALMS_QTEST_MAIN_IMPL(TestClass, QApplication)

#define WILDPALMS_QTEST_GUILESS_MAIN(TestClass) \
    WILDPALMS_QTEST_MAIN_IMPL(TestClass, QCoreApplication)
