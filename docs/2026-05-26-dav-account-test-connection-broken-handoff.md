# Handoff: "Add Account → Test Connection" always says "Failed" (DAV providers)

**Date:** 2026-05-26
**Status:** Diagnosed, root cause in **WildPalms** (not libkalburator). Unfixed.
**Severity:** Blocker — no DAV account (CalDAV / CardDAV / Multi-protocol DAV)
can be tested *or saved* through the current Add Account form.

---

## TL;DR

`WildPalms/src/app/accounts/accountformwidget.cpp` builds a provider and its
config widget but **never copies the user's typed values out of the widget and
into the provider.** So when you click *Test Connection*, the provider's
`m_serverUrl` is still empty, `IProvider::connect()` hits its
"no server URL" guard, returns `false` immediately, and the form prints a bare
`"Failed"` — with no network request, no discovery, and no error text.

This is why recent libkalburator fixes (RFC 6764 `.well-known` discovery,
per-protocol error surfacing, `kalburator.sync.multidav` logging) had **zero
observable effect**: the code path that would run them is never reached.

The same gap means **Save persists an empty/invalid account** (see §5).

---

## Symptom

Add Account → "Multi-protocol DAV (calendar + contacts)" → fill in Display
name / Server URL (`https://my.opendesktop.org`) / Username / Password →
*Test Connection* → label flips to **"Failed"** instantly. No console output,
no way to tell whether it was the URL, the credentials, CalDAV vs CardDAV, etc.

---

## Root cause (exact)

`src/app/accounts/accountformwidget.cpp`:

**Build phase — provider and widget created, never linked:**
```cpp
// buildUi(), lines ~61-65
auto provider = contribution->createProvider(this);
QWidget *cfg = provider ? provider->createConfigWidget(m_configStack) : nullptr;
if (!cfg) cfg = new QWidget(m_configStack);
m_configStack->addWidget(cfg);          // widget holds the user's input
m_providers.push_back(std::move(provider));  // provider stays empty
```

**Test phase — connects an empty provider:**
```cpp
// onTestConnection(), lines ~108-124
IProvider *p = m_providers.at(idx).get();
m_statusLabel->setText(tr("Testing..."));
auto fut = p->connect();                // <-- provider config never loaded
auto *w = new QFutureWatcher<bool>(this);
connect(w, &QFutureWatcher<bool>::finished, this, [this, w, p]() {
    const bool ok = w->result();
    m_statusLabel->setText(ok ? tr("Connected") : tr("Failed"));  // bare "Failed"
    p->disconnect();
    w->deleteLater();
});
w->setFuture(fut);
```

In libkalburator, `MultiProtocolDavProvider::connect()` begins with:
```cpp
if (m_serverUrl.isEmpty() || !m_serverUrl.isValid()) {
    emit error(QStringLiteral("No server URL configured."));
    // ... returns an immediately-resolved false future
}
```
`m_serverUrl` is empty because nobody called `provider->load(...)` with the
widget's contents. The `error()` signal *is* emitted with a reason — but
`onTestConnection()` never connects to `IProvider::error(QString)`, so the
reason is discarded and only `"Failed"` is shown.

---

## Why this is a design flaw, not a one-line oversight

libkalburator currently ships **two incompatible config-widget contracts**, and
`AccountFormWidget` bridges *neither*:

| Widget | Construction | How edits reach the provider |
|---|---|---|
| `CalDavConfigWidget` | `CalDavConfigWidget(provider, parent)` — holds a provider ptr | `applyToProvider()` (widget → provider). Has its own internal Test button in the single-provider flow. |
| `MultiProtocolDavConfigWidget` | `MultiProtocolDavConfigWidget(parent)` — **no provider ptr** | `configuration()` returns a `BackendConfiguration`. `createConfigWidget()` only does `setConfiguration(save())` (provider → widget). **No widget → provider path exists.** |
| `CardDavConfigWidget` | — | `CardDavProvider::createConfigWidget()` returns **nullptr** (not implemented). |

`AccountFormWidget` treats every config widget as an opaque `QWidget*`. It never
calls `applyToProvider()` (CalDAV) and never calls `configuration()` +
`provider->load()` (multiproto). There is **no common interface** that lets a
generic consumer pull edited config out of an arbitrary provider's widget.

Consequence: the multi-account form is broken for **every** DAV kind, not just
multiproto.

---

## Recommended fix

### Sound fix (preferred) — give config widgets one contract

In **libkalburator**, introduce a minimal interface (header-only is fine):

```cpp
// src/sync/iproviderconfigwidget.h
namespace Kalburator::Sync {
class IProviderConfigWidget {
public:
    virtual ~IProviderConfigWidget() = default;
    virtual BackendConfiguration configuration() const = 0;
    virtual void setConfiguration(const BackendConfiguration &) = 0;
};
}
```

- `MultiProtocolDavConfigWidget` already has both methods — just inherit the
  interface (multiple inheritance with `QWidget`).
- `CalDavConfigWidget` / `AkonadiConfigWidget` implement `configuration()`
  (read fields → `BackendConfiguration`) and `setConfiguration()`; the existing
  `applyToProvider()` can become `provider->load(configuration())` or be dropped.
- `CardDavProvider::createConfigWidget()` should return a real widget.

Then **WildPalms** `AccountFormWidget` bridges once, generically, before both
testing and saving:

```cpp
// before p->connect() in onTestConnection(), and inside configuration():
auto *cw = dynamic_cast<Kalburator::Sync::IProviderConfigWidget*>(
               m_configStack->currentWidget());
if (cw) p->load(cw->configuration());
```

Also in `onTestConnection()`, connect to the provider's error signal so the
status label shows the reason instead of "Failed":
```cpp
QObject::connect(p, &IProvider::error, this, [this](const QString &msg){
    m_lastTestError = msg;
});
// ...on finish:
m_statusLabel->setText(ok ? tr("Connected")
                          : tr("Failed: %1").arg(m_lastTestError));
```
(And consider showing `p->lastWarning()` on success — multiproto reports a
partial-protocol failure there, e.g. CalDAV worked but CardDAV didn't.)

### Quick unblock (WildPalms-only, no libkalburator change)

`MultiProtocolDavConfigWidget::configuration()` already exists, so for the
multiproto kind you can bridge with a type-specific cast as a stopgap:
```cpp
if (auto *mw = dynamic_cast<Kalburator::Sync::MultiProtocolDavConfigWidget*>(
                   m_configStack->currentWidget()))
    p->load(mw->configuration());
```
This unblocks multiproto testing immediately but does not fix CalDAV and is not
the durable design — prefer the interface above.

---

## Also broken by the same gap: Save

`AccountFormWidget::configuration()` (lines ~126-132) returns `p->save()`, i.e.
it serializes the **provider's** state — which is empty for the same reason.
So even if the user ignores Test Connection and clicks Save, the persisted
account has no URL/credentials. The same widget→provider bridge fixes this:
`configuration()` must `load()` from the current widget before calling
`p->save()` (or read the widget's `configuration()` directly).

`isValid()` (returns `cfg.isValid() && !displayName.isEmpty()`) is therefore
also always false for multiproto — worth checking whether the Save button is
enabled at all.

---

## Verification (after fixing)

1. Add Account → Multi-protocol DAV, enter `https://my.opendesktop.org` + real
   credentials → Test Connection.
2. Expect either "Connected — N collection(s)" or a *specific* failure
   ("Failed: ...PROPFIND failed (HTTP 401)"), and matching
   `kalburator.sync.multidav` lines on the console
   (`QT_LOGGING_RULES="kalburator.sync.multidav.debug=true"` if needed).
3. Confirm Save persists a config with the URL/credentials present.
4. If you still get HTTP 401 after the URL is correctly loaded: opendesktop.org
   / NextCloud with 2FA needs an **app password**, not the login password —
   that is a *user-config* issue, and the new error text will say so.

---

## Notes for whoever picks this up

- **libkalburator dependency pin:** WildPalms fetches libkalburator via
  FetchContent (`build-dev/_deps/libkalburator-src`). The `.well-known` /
  error-surfacing / logging fixes landed on libkalburator `main` at commits
  `4785933` and `4511787`. The `IProviderConfigWidget` interface above does not
  exist yet — it would be a new libkalburator change requiring a pin bump here.
- libkalburator's own `Kalburator::Ui::ProviderConfigDialog` was given a status
  label + error capture in `4511787`, but **WildPalms does not use that dialog**
  — it has its own `AccountFormWidget`. Either adopt `ProviderConfigDialog`
  (which would inherit the fix) or replicate the bridge + error display in
  `AccountFormWidget`. Adopting the shared dialog is the smaller long-term
  surface.
- The libkalburator-side discovery (RFC 6764 bare-host `.well-known` bootstrap)
  is verified working against a NextCloud-topology fixture; it is *not* the
  cause of this failure and cannot be exercised until the config-load bug here
  is fixed.
