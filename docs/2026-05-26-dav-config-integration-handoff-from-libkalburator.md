# Handoff → WildPalms: integrate the DAV config-widget contract (libkalburator done)

**Date:** 2026-05-26
**From:** libkalburator dev
**To:** WildPalms (`src/app/accounts/accountformwidget.cpp`)
**Against:** libkalburator `main` @ `387e8e3`
**Answers:** your `docs/2026-05-26-carddav-config-widget-handoff-libkalburator.md`
**Supersedes:** `docs/2026-05-26-dav-account-test-connection-broken-handoff.md` (stale)

---

## TL;DR

Everything you asked libkalburator for is now on `main`. The DAV config-widget
contract is complete and uniform across all kinds. **The remaining work is
entirely on the WildPalms side: `AccountFormWidget` must bridge the embedded
config widget into the provider before `connect()`/`save()`, and surface
`error()`.** Bump your libkalburator pin to `387e8e3` (or `main`) first.

Today `AccountFormWidget` never copies the user's input out of the config widget
into the provider, so `connect()` runs on an empty provider, fails its
"no server URL configured" guard instantly, and prints a bare "Failed". That is
the whole bug. The fix is ~5 lines + error wiring, shown below.

---

## What landed in libkalburator (consume these)

| Commit | What it gives you |
|---|---|
| `4785933` | RFC 6764 `.well-known/{caldav,carddav}` discovery — **bare-host URLs work** (`https://my.opendesktop.org`, no `/remote.php/dav` needed). Manual-principal override honored. |
| `4511787` | Providers emit real `error(QString)` (discovery reason + HTTP status); `MultiProtocolDavProvider` logs per-protocol outcomes to `kalburator.sync.multidav`; partial success via `lastWarning()`. |
| `a342a04` | **`Kalburator::Sync::IProviderConfigWidget`** (`configuration()` / `setConfiguration()`) — the uniform bridge contract. All existing config widgets conform. `ProviderConfigDialog` bridges + shows errors (reference impl). |
| `387e8e3` | **`CardDavConfigWidget`** — `CardDavProvider::createConfigWidget()` now returns a real, conforming widget. Standalone CardDAV is configurable. |

Per-kind config widget status — all conform to `IProviderConfigWidget`:

| Kind | Widget | Notes |
|---|---|---|
| `caldav` | `CalDavConfigWidget` | conforms; **also has its own internal Test button** (heavy pattern — see caveat) |
| `carddav` | `CardDavConfigWidget` | lean; new in `387e8e3` |
| `multiproto-dav` | `MultiProtocolDavConfigWidget` | lean |
| `akonadi` | `AkonadiConfigWidget` | lean (gated on `HAVE_AKONADI`) |

---

## What WildPalms must do

`src/app/accounts/accountformwidget.cpp`. Two call sites currently read/run the
provider without first loading the widget's values into it.

### 1. Bridge before connecting (`onTestConnection()`, ~line 115)

```cpp
#include <iproviderconfigwidget.h>   // add to includes

void AccountFormWidget::onTestConnection() {
    const int idx = m_kindCombo->currentIndex();
    if (idx < 0 || static_cast<std::size_t>(idx) >= m_providers.size()) return;
    IProvider *p = m_providers.at(static_cast<std::size_t>(idx)).get();
    if (!p) return;

    // Bridge: widget -> provider, before connect(). Without this the provider
    // is empty and connect() fails its "no server URL" guard.
    if (auto *cw = dynamic_cast<Kalburator::Sync::IProviderConfigWidget *>(
                       m_configStack->currentWidget()))
        p->load(cw->configuration());

    m_statusLabel->setText(tr("Testing..."));

    // Capture the reason so the label can say more than "Failed".
    m_lastTestError.clear();
    QObject::disconnect(m_errorConn);
    m_errorConn = connect(p, &IProvider::error, this,
                          [this](const QString &msg){ m_lastTestError = msg; });

    auto fut = p->connect();
    auto *w = new QFutureWatcher<bool>(this);
    connect(w, &QFutureWatcher<bool>::finished, this, [this, w, p]() {
        const bool ok = w->result();
        QObject::disconnect(m_errorConn);
        if (ok) {
            QString msg = tr("Connected");
            const QString warn = p->lastWarning();   // partial-protocol failure
            if (!warn.isEmpty()) msg += tr(" — %1").arg(warn);
            m_statusLabel->setText(msg);
        } else {
            m_statusLabel->setText(m_lastTestError.isEmpty()
                ? tr("Failed") : tr("Failed: %1").arg(m_lastTestError));
        }
        p->disconnect();
        w->deleteLater();
    });
    w->setFuture(fut);
}
```

Add members to `accountformwidget.h`: `QString m_lastTestError;` and
`QMetaObject::Connection m_errorConn;`.

### 2. Bridge before saving (`configuration()`, ~line 126)

```cpp
BackendConfiguration AccountFormWidget::configuration() const {
    const int idx = m_kindCombo->currentIndex();
    if (idx < 0 || static_cast<std::size_t>(idx) >= m_providers.size()) return {};
    IProvider *p = m_providers.at(static_cast<std::size_t>(idx)).get();
    if (!p) return {};
    if (auto *cw = dynamic_cast<Kalburator::Sync::IProviderConfigWidget *>(
                       m_configStack->currentWidget()))
        p->load(cw->configuration());   // widget -> provider before serialize
    return p->save();
}
```

(`m_configStack->currentWidget()` is valid in a const method; `dynamic_cast` is
fine because `QWidget` is polymorphic. `unique_ptr::operator->` is usable in a
const method.)

That's the whole fix. `ProviderConfigDialog::applyWidgetToProvider()` in
libkalburator (`src/ui/providerconfigdialog.cpp`) is the exact reference if you
prefer to read working code — or you could adopt `ProviderConfigDialog` wholesale
and delete `AccountFormWidget`'s bespoke logic (it already does bridge + status +
error). Your call; the small patch above is the minimal change.

---

## One caveat: the CalDAV double Test button (your "idealistic note")

`CalDavConfigWidget` still uses the **heavy pattern** — it owns an internal
"Test Connection" button + status label. Once `AccountFormWidget` supplies its
own Test button (above), the **CalDAV kind will show two Test buttons**; every
other kind shows one.

This is *not* fixed yet on the libkalburator side — lean-ifying
`CalDavConfigWidget` is a behavior change plus a rewrite of
`tst_caldav_config_widget.cpp` (it asserts the heavy API:
`CalDavConfigWidget(provider,…)`, `testButtonForTesting()`, `applyToProvider()`).
It was explicitly optional in your handoff, so it's **deferred pending a
decision**. Options:

- **libkalburator lean-ifies CalDav** (drop provider ptr + internal Test button,
  keep `configuration()`/`setConfiguration()`, `createConfigWidget` → `new
  CalDavConfigWidget(parent)` + `setConfiguration(save())`). One uniform pattern,
  no double button. Say the word and it's a quick follow-up commit.
- **WildPalms hides the embedded widget's Test button** for the caldav kind
  (stopgap).

Recommend the first. Flagging so the double button doesn't surprise you in
testing.

---

## Verification (after pin bump + the WP patch)

1. Add Account → **Multi-protocol DAV** → `https://my.opendesktop.org` + creds →
   Test Connection. Expect `Connected — …` or a *specific* reason
   (`Failed: …PROPFIND failed (HTTP 401)`), plus `kalburator.sync.multidav` lines
   on the console (`QT_LOGGING_RULES="kalburator.sync.multidav.debug=true"`).
2. Same for **CalDAV (calendar)** and **CardDAV (contacts)** — all three forms
   now populate and the provider receives the typed URL/credentials.
3. Save persists a config with url/username/password present (not empty);
   `isValid()` is true.
4. If you still get HTTP 401 against opendesktop with the URL correctly loaded:
   that's a **user-config** matter — NextCloud with 2FA needs an *app password*,
   not the login password. The surfaced error will now say so, instead of a
   bare "Failed".
