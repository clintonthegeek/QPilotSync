# Handoff → libkalburator: implement `CardDavConfigWidget` (last DAV config-widget gap)

**Date:** 2026-05-26
**From:** WildPalms (consumer of `Kalburator::Sync` providers)
**To:** libkalburator dev
**Against:** libkalburator `main` @ `4511787` (fix(dav): surface connection errors + honor manual DAV principals)
**Supersedes:** `docs/2026-05-26-dav-account-test-connection-broken-handoff.md` — that
doc is **stale**. It was written before the `IProviderConfigWidget` interface
landed and assumed the whole bridge was missing. Most of it is already done
(see "Current state"). This doc records the *one* remaining libkalburator gap.

---

## TL;DR

The `IProviderConfigWidget` bridge contract exists and works. CalDAV,
Multi-protocol DAV, and Akonadi config widgets all conform; `ProviderConfigDialog`
bridges them correctly. **The only remaining gap in libkalburator is that
`CardDavProvider::createConfigWidget()` still returns `nullptr`** — there is no
`CardDavConfigWidget`. Consequence: a **CardDAV-alone** account (contacts only)
cannot be configured, tested, or saved through *any* consumer, because there is
no widget to type a URL/credentials into.

This blocks WildPalms from offering CardDAV-alone in its Add-Account form. (The
Multi-protocol DAV path already covers Nextcloud calendar+contacts, so this is
specifically about the standalone CardDAV kind.)

**Ask:** implement `CardDavConfigWidget` in the lean pattern (matching
`MultiProtocolDavConfigWidget` / `AkonadiConfigWidget`) and wire it into
`CardDavProvider::createConfigWidget()`. Full spec + code below.

---

## Current state (verified at `4511787`)

| Piece | Status |
|---|---|
| `src/sync/iproviderconfigwidget.h` — `configuration()` / `setConfiguration()` contract | ✅ exists |
| `CalDavConfigWidget` conforms | ✅ (heavy pattern — holds provider ptr + own Test button) |
| `MultiProtocolDavConfigWidget` conforms | ✅ (lean pattern) |
| `AkonadiConfigWidget` conforms | ✅ (lean pattern) |
| `ProviderConfigDialog` bridges widget→provider | ✅ `applyWidgetToProvider()` called before `connect()` (l.199) and `result()` (l.258); error surfacing done |
| **`CardDavConfigWidget`** | ❌ **does not exist** — `CardDavProvider::createConfigWidget()` returns `nullptr` (`src/sync/carddavprovider.cpp:41-44`) |

`CardDavProvider` is otherwise complete: `load()`/`save()` round-trip
`url` / `username` / `password` (`carddavprovider.cpp:22-39`), and `connect()`
runs real `CardDavCapabilityDiscovery` (PROPFIND principal → addressbook-home-set)
and emits `error(QString)` with reasons. It just has no form widget.

---

## The ask — `CardDavConfigWidget` (lean pattern)

Model it on `MultiProtocolDavConfigWidget` (a dumb form; the consuming dialog
owns Test/connect/error). **Not** on `CalDavConfigWidget` — see "Idealistic note"
for why the heavy pattern is the one we'd like to retire, not propagate.

### `src/sync/carddavconfigwidget.h` (new)

```cpp
#ifndef KALBURATOR_SYNC_CARDDAVCONFIGWIDGET_H
#define KALBURATOR_SYNC_CARDDAVCONFIGWIDGET_H

#include "backendconfiguration.h"
#include "iproviderconfigwidget.h"
#include <QWidget>

class QLineEdit;

namespace Kalburator::Sync {

/**
 * @brief Form widget for editing a CardDavProvider's account config.
 *
 * A dumb form: displayName / server URL / username / password, exposed via
 * IProviderConfigWidget. Testing, connecting and error reporting belong to the
 * consuming dialog (ProviderConfigDialog or WildPalms' AccountFormWidget),
 * which bridges widget->provider via provider->load(configuration()) before
 * connect()/save().
 */
class CardDavConfigWidget : public QWidget,
                            public IProviderConfigWidget
{
    Q_OBJECT
public:
    explicit CardDavConfigWidget(QWidget *parent = nullptr);

    BackendConfiguration configuration() const override;
    void setConfiguration(const BackendConfiguration &cfg) override;

private:
    QLineEdit *m_displayNameEdit;
    QLineEdit *m_urlEdit;
    QLineEdit *m_usernameEdit;
    QLineEdit *m_passwordEdit;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_CARDDAVCONFIGWIDGET_H
```

### `src/sync/carddavconfigwidget.cpp` (new)

```cpp
#include "carddavconfigwidget.h"

#include <QFormLayout>
#include <QLineEdit>

namespace Kalburator::Sync {

CardDavConfigWidget::CardDavConfigWidget(QWidget *parent)
    : QWidget(parent)
    , m_displayNameEdit(new QLineEdit(this))
    , m_urlEdit(new QLineEdit(this))
    , m_usernameEdit(new QLineEdit(this))
    , m_passwordEdit(new QLineEdit(this))
{
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_displayNameEdit->setPlaceholderText(tr("My contacts"));
    // Bare host is fine — RFC 6764 .well-known discovery (4785933) resolves it.
    m_urlEdit->setPlaceholderText(tr("https://cloud.example.com"));

    auto *form = new QFormLayout(this);
    form->addRow(tr("Display name:"), m_displayNameEdit);
    form->addRow(tr("Server URL:"),   m_urlEdit);
    form->addRow(tr("Username:"),     m_usernameEdit);
    form->addRow(tr("Password:"),     m_passwordEdit);
}

void CardDavConfigWidget::setConfiguration(const BackendConfiguration &cfg)
{
    m_displayNameEdit->setText(cfg.displayName);
    const auto &p = cfg.connectionParams;
    m_urlEdit->setText(p.value(QStringLiteral("url")).toString());
    m_usernameEdit->setText(p.value(QStringLiteral("username")).toString());
    m_passwordEdit->setText(p.value(QStringLiteral("password")).toString());
}

BackendConfiguration CardDavConfigWidget::configuration() const
{
    BackendConfiguration cfg;
    cfg.type        = QStringLiteral("carddav");   // matches CardDavProvider::kind()
    cfg.displayName = m_displayNameEdit->text();
    cfg.connectionParams[QStringLiteral("url")]      = m_urlEdit->text();
    cfg.connectionParams[QStringLiteral("username")] = m_usernameEdit->text();
    cfg.connectionParams[QStringLiteral("password")] = m_passwordEdit->text();
    return cfg;
}

} // namespace Kalburator::Sync
```

> Note: `configuration()` leaves `cfg.id` empty on purpose. `IProvider::load()`
> only overwrites `m_id` when `cfg.id` is non-empty (`carddavprovider.cpp:23`),
> so the provider keeps its generated UUID — same convention as
> `MultiProtocolDavConfigWidget`.

### Wire it up — `src/sync/carddavprovider.cpp`

```cpp
#include "carddavconfigwidget.h"   // add to includes
// ...
QWidget *CardDavProvider::createConfigWidget(QWidget *parent) {
    auto *w = new CardDavConfigWidget(parent);
    w->setConfiguration(save());   // provider -> widget, same as CalDav/multiproto
    return w;
}
```

### CMake — `CMakeLists.txt`

Add alongside the existing `*configwidget` entries:
- header into the sync HEADERS list (near `src/sync/caldavconfigwidget.h`, ~l.479 / `multiprotocoldavconfigwidget.h` ~l.497):
  `src/sync/carddavconfigwidget.h`
- source into the sync SOURCES list (near `src/sync/caldavconfigwidget.cpp` ~l.506 / `multiprotocoldavconfigwidget.cpp` ~l.522):
  `src/sync/carddavconfigwidget.cpp`

(It's `Q_OBJECT`, so AUTOMOC handles it; no extra moc wiring.)

### Test — `tests/sync/tst_carddav_config_widget.cpp` (new)

Mirror the config round-trip half of `tst_caldav_config_widget.cpp` (drop the
Test-button assertions — the lean widget has none):

1. `setConfiguration(cfg)` populates the four fields.
2. `configuration()` returns a `BackendConfiguration` with `type=="carddav"`,
   the typed `displayName`, and `url`/`username`/`password` in
   `connectionParams`.
3. Round-trip: `setConfiguration(x); QCOMPARE(configuration().connectionParams, x.connectionParams)`.
4. `CardDavProvider::createConfigWidget()` returns non-null and is
   `dynamic_cast<IProviderConfigWidget*>`-able.

Register it in `tests/sync/CMakeLists.txt` next to the caldav widget test.

---

## Idealistic note (optional, your call) — retire CalDAV's heavy pattern

`CalDavConfigWidget` is the lone widget still using the **heavy pattern**: it
holds a `CalDavProvider*` and carries its *own* internal "Test Connection"
button + status label (`caldavconfigwidget.{h,cpp}`). The lean siblings
(multiproto, akonadi, and the proposed carddav) don't.

This produces a concrete wart in WildPalms' `AccountFormWidget`, which embeds
the provider's widget **and** supplies its own Test button: for CalDAV the user
sees **two** Test buttons; for every other kind, one. Test/connect/error now
live in the consumers (`ProviderConfigDialog` ✅, `AccountFormWidget` — landing
on the WP side), so CalDAV's embedded copy is redundant.

**Idealistic recommendation:** lean-ify `CalDavConfigWidget` to match — drop the
provider ptr, internal Test button, `onTestClicked/onTestFinished`,
`applyToProvider()`; keep `configuration()`/`setConfiguration()`; change
`CalDavProvider::createConfigWidget()` to `new CalDavConfigWidget(parent)` +
`setConfiguration(save())`. **Blast radius:** `tests/sync/tst_caldav_config_widget.cpp`
(7 references to the heavy API — `CalDavConfigWidget(provider, …)`,
`testButtonForTesting()`, `applyToProvider()`) would need rewriting to the
config-round-trip shape. Not required to unblock CardDAV; flagged because you
asked for the ideal end-state (one uniform widget contract).

---

## WildPalms side (for context — not your task)

WP's `src/app/accounts/accountformwidget.cpp` is the consumer that's currently
broken for *all* DAV kinds: it never bridges the embedded widget into the
provider, so `Test Connection` always says a bare "Failed". WP will fix this on
its own side by copying `ProviderConfigDialog`'s pattern:

```cpp
// before p->connect() in onTestConnection(), and before p->save() in configuration():
if (auto *cw = dynamic_cast<Kalburator::Sync::IProviderConfigWidget *>(
                   m_configStack->currentWidget()))
    p->load(cw->configuration());
```

…plus connecting `IProvider::error` to show `Failed: <reason>` and surfacing
`lastWarning()` on partial success. This WP change works *today* for caldav /
multiproto / akonadi; it only reaches CardDAV once `createConfigWidget()` above
returns a real widget.

---

## Verification (after the libkalburator change + WP pin bump)

1. WildPalms → Add Account → **CardDAV (contacts)**: the form now shows
   Display name / Server URL / Username / Password fields (today it's blank).
2. Enter `https://my.opendesktop.org` + credentials → Test Connection →
   expect `Connected — N collection(s)` or a *specific* failure
   (`Failed: …PROPFIND failed (HTTP 401)`), with `kalburator.sync` log lines.
3. Save persists a config with url/username/password present (not empty).
4. `ctest -R carddav` green (new widget test + existing carddav e2e unaffected).
