# Archived documentation

These docs describe earlier states of Wild Palms that no longer match
the codebase. They are kept for historical context — refer to current
docs for present-day behaviour.

## Inventory

| File | Archived | Why |
|---|---|---|
| `ARCHITECTURE.md` | 2026-03 | Pre-2026 architecture write-up; superseded by `docs/ARCHITECTURE_2026.md`. |
| `CHALLENGES.md` | 2026-03 | Early scoping notes; superseded by inline phase plans. |
| `STATUS_2026-02-17.md` | 2026-03 | First-HotSync milestone snapshot. |
| `ROADMAP.md` | 2026-05-21 | Original Phases 1–6 (Foundation → Release). Phases 1–4 shipped; Phases 5–6 work has migrated into `docs/plans/2026-04-20-libkalburator-integration.md` and its sub-phase tracker `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`. The old roadmap structure no longer maps to how work is now scheduled. |
| `CONDUIT_PLUGIN_DESIGN.md` | 2026-05-21 | RFC for the `IConduit` family. All `IConduit` halves were deleted in E.16 (2026-04-28). Replaced by the `IPlugin` / `IBackendPlugin` / `IPluginAction` ABI; see Phase-E spec for the canonical description. |
| `TODO-conduit-code-sharing.md` | 2026-05-21 | Refactoring opportunities across the old `*Conduit` files; those files no longer exist. |
| `TODO-plugin-output-handling.md` | 2026-05-21 | Refers to `pluckerconduit.cpp:spiderChannel()`, deleted in E.16. The underlying principle (capture full subprocess output) may still apply to `PluckerBackendPlugin`, but should be re-filed against current code if relevant. |
| `TODO-webcalendar-conduit.md` | 2026-05-21 | Describes gaps in `WebCalendarConduit`, deleted in E.16. WebCalendar now ships as `WebcalBackendPlugin` (Phase E.13). |
| `plugin-developer-guide.md` | 2026-05-21 | "Database claim system" / `ISyncConduit` developer guide. The claim system and `ISyncConduit` are gone. To be replaced by `docs/PLUGIN_ABI.md` in Phase E.19. |
| `sdk-plugin-guide.md` | 2026-05-21 | SDK guide for the old `Sync::SyncConduitBase` consumer pattern. Phase E rewrote the ABI; there is no shipping SDK at present. |
| `sdk-shadowstan-advisory.md` | 2026-05-21 | Linking workarounds for the old SDK published to ShadowStan. Obsolete with the SDK retirement. |
