# Phase 5 handoff: adopt libkalburator's `note` domain for the memo conduit

**Date:** 2026-05-25
**For:** the next agent picking up Phase 5 (memo) of the libkalburator canon adoption.
**Status:** libkalburator has **delivered** the capability we requested. WildPalms
adoption has **not started**. Your job: investigate what they shipped, then write and
execute the WildPalms adoption plan (mirroring Phase 3/4).

---

## 1. Where this sits

WildPalms is adopting libkalburator's shape-graph canon convergence, one PIM domain at a
time, on branch `feature/canon-adoption-phase1`:

- Phase 1 track-to-green ✅, Phase 2 contacts ✅, Phase 3 calendar ✅, Phase 4 todo ✅
  (suite 98/98, all unpushed).
- **Phase 5 = memo**, the last domain. It is **different** from the other three (see §3),
  which is why we first sent libkalburator a requirements doc and they built new domain
  surface for it.

Read these first, in order:

1. `docs/2026-05-25-document-domain-requirements-for-libkalburator.md` — what we asked
   libkalburator for (capabilities/contracts; we deliberately left the canonical's shape and
   naming to them).
2. `docs/superpowers/plans/2026-05-24-phase4-todo-canon-migration.md` — the **template**
   for a WildPalms domain adoption (stages, DomainExtension, backend flip, verification
   test, submodule discipline). Phase 5 mirrors this, with the §4 additions.
3. The libkalburator design + plan for what they built (in the sibling repo):
   `../libkalburator/docs/2026-05-25-note-domain-design.md` and `-note-domain-plan.md`.

Project memory worth loading: `project_phase4_todo_canon`,
`project_typed_routing_and_abandoned_queue`, `feedback_library_vs_backend_responsibility`.

---

## 2. What libkalburator delivered (investigate; don't trust this summary blindly)

They named the domain **`note`** (not `document`/`memo` — naming was explicitly their call).
Branch: `feature/canon-upgrade-convergence` (the sibling `../libkalburator`, already
checked out there). Relevant commits: `9e59940`, `43d3a6e`, `904c9af`, `cc70369`, `b629c8a`.

Delivered surface (verify the exact APIs by reading these files):

- **`../libkalburator/src/note/`** — the domain:
  - `notedomaindefinition.{h,cpp}` — domain `"note"`, **canonical `(note, canon)`**,
    canonical catalogue props `{uid, body, categories, lastmodified}` (`noteproperties.cpp`),
    TextDiffer/TextMerger on the canonical.
  - `notestockshapes.{h,cpp}` — registers the **`(note, markdown)` peer** and the
    `markdown ↔ canon` edges. **This is the piece the old `memo` domain lacked.**
  - `markdowncanonstages.{h,cpp}` — the `markdown ↔ canon` stages. **Key contract for us:**
    the entire leading YAML frontmatter block (`---\n…\n---`) is carried **verbatim** into
    `providerExtras["frontmatter"]` in the canon (Reversible) and reconstructed on the way
    back; the canon `body` is the markdown sans frontmatter; `uid` is parsed out of the
    frontmatter. **This is the round-trip channel our Palm identity rides in** — confirm it
    preserves arbitrary frontmatter keys (e.g. our `X-WP-PALM-RECORDID`, category slot).
- **`../libkalburator/src/universal/markdownfilesbackend.{h,cpp}`** — `MarkdownFilesBackend`
  (a `RawFilesBackend` subclass): writes one **`.md`** per record, filename stem = first
  non-empty body line after frontmatter (sanitised; fallback `note_<recordId>`), bytes
  written are the `(note, markdown)` peer encoding verbatim. Construct with a root path,
  then `createCollection(info, {DomainId{"note"}, EncodingId{"markdown"}})`.
  - They also refactored `RawFilesBackend` to expose `suffixFor`/`recordStem` as virtual
    seams (`cc70369`) — no behavior change to existing sinks.

**First investigation task:** read the four `src/note/*` headers + `markdownfilesbackend.h`
and the design doc, and confirm: the markdown peer's frontmatter round-trip keys are
arbitrary/preserved; how `categories` maps (our Palm category slot vs the canon
`categories` list); and how `MarkdownFilesBackend` is meant to be constructed/wired.

---

## 3. Why memo differs from calendar/todo (so you don't just blind-mirror Phase 4)

- The old libkalburator `memo` domain was thin (`(memo, text)`, no peers, no edges) — that
  is why our memo backend never went on the shape graph and instead declared `(blob, raw)`
  and pre-transcoded to Markdown internally.
- WildPalms has a **human-readable Markdown-on-disk feature**: `memoview.cpp` reads `*.md`
  files with YAML frontmatter (`id:`, `category:`). The whole point of asking for the `note`
  domain + `MarkdownFilesBackend` was to **keep that feature** while putting memo on the
  shape graph — instead of the generic `RawFilesBackend` mirroring the source shape and
  writing Palm wire bytes to disk (which is what calendar/todo now do — see §6).
- So Phase 5 has **one extra moving part** beyond the Phase 4 template: the memo sync's
  on-disk peer must become `MarkdownFilesBackend` declaring `(note, markdown)`, not the
  default shape-mirroring `RawFilesBackend`.

---

## 4. The WildPalms adoption (what you'll plan + build)

Mirror the Phase 4 todo migration, with the noted additions. The memo plugin is the
submodule `src/plugins/memo` (`wildpalms-conduit-memo`) on branch
`feature/canon-adoption-phase1`. Relevant existing files: `memobackendplugin.{h,cpp}`,
`memoblobbackend.{h,cpp}`, `memomarkdown.{h,cpp}`, `memoview.cpp`, `CMakeLists.txt`;
tests under `tests/plugins/memo/`.

Expected shape of the work (confirm against your investigation before finalizing):

1. **`NoteDomainExtension`** (WildPalms side, mirror `CalendarDomainExtension`/
   `TodoDomainExtension`): register `(note, palm)` peer + `(note, palm) ↔ (note, markdown)`
   edges. The stage bodies reuse our existing `memomarkdown` encode/decode to produce/parse
   Markdown **with our frontmatter** (recordId + category slot + private flag) — that
   frontmatter then rides through libkalburator's `markdown ↔ canon` verbatim. libkalburator
   provides `markdown ↔ canon`; we provide `palm ↔ markdown`. Honest loss: palm→markdown
   ~lossless; markdown→palm `Simplified` (Markdown structure flattened to Palm plain text).
2. **Flip `MemoBlobBackend`** to declare `(note, palm)` and present raw `PalmRecord` wire
   bytes (`br.type` = "note"? confirm convention), consuming wire bytes in
   create/update — exactly the Phase 4 backend diff.
3. **Register the extension** in `MemoBackendPlugin`'s ctor (mirror Phase 4 Task 4).
4. **Wire the Markdown sink:** in `src/runtime/palmruntime.cpp`, the memo mapping's on-disk
   peer must be a `MarkdownFilesBackend` declaring `(note, markdown)` instead of the
   default `RawFilesBackend` created with the source shape (`palmruntime.cpp:355-363`). This
   is the Phase-5-specific change with no Phase-4 analogue — design it carefully (per-domain
   sink selection in `finishConnect`).
5. **Verification test** `tst_memo_note_roundtrip` (mirror `tst_todo_canon_roundtrip`):
   `palm → canon → palm` preserves body + Palm identity (recordId/category); loss honest.
   Plus update `tst_memoblobbackend` for the wire-bytes contract (mirror Phase 4 Task 7).
6. **Once green and relying on libkalburator's markdown stages + sink, delete our redundant
   code** (`memomarkdown` and the internal transcoding in `MemoBlobBackend`) per the
   requirements doc's stated goal — but only the parts genuinely superseded; our
   `palm ↔ markdown` edge may still need a thin encode/decode. Decide during implementation.
7. **`memoview.cpp` compatibility:** it currently reads `*.md` with `id:`/`category:`
   frontmatter. Confirm `MarkdownFilesBackend`'s output (filename + frontmatter keys) is
   what the view expects, or note the view delta. Existing on-disk `.md` files from the old
   path may use different frontmatter conventions — check back-compat.

---

## 5. Build / test / discipline

- Dev build dir `build-dev`, configured against the sibling: reconfigure if needed with
  `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR=$(realpath ../libkalburator)`.
  Make sure `../libkalburator` is on `feature/canon-upgrade-convergence` and rebuilt.
- Submodule edits (`src/plugins/memo/*`) are committed **inside the submodule**; superproject
  test edits, the `palmruntime.cpp` change, and the pointer bump are superproject commits.
- Suite baseline is **98** (Phase 4). Phase 5 adds the new memo round-trip test; keep the
  whole suite green.
- **Do NOT push** (the controller gates pushes) and **do NOT merge to WildPalms `main` / ship**
  until libkalburator's canon convergence merges to *its* `main` and is device-verified.
  Same rule as Phases 1–4.

---

## 6. Side note you should be aware of (not Phase 5 scope)

While investigating Phase 5, we confirmed that after Phases 3/4 the generic `RawFilesBackend`
peer mirrors the *source* shape (`palmruntime.cpp:355-363`), so calendar/todo now write
**Palm wire bytes** to disk, while `calendarview.cpp`/`taskview.cpp` still read `*.ics` via
`ICalFormat`. Their on-disk *display* reading is therefore out of step with what the sync
writes — this is deferred "Phase 6 (loss-UX)" work. The memo Markdown sink (§4.4) is the
first instance of the general fix (an on-disk peer that declares a *readable* presentation
shape). Don't try to fix calendar/todo views in Phase 5; just don't be surprised by it.
