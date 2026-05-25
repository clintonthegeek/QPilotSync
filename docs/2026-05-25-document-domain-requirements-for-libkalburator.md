# Requirements: a `document` domain, Markdown peer, and Markdown-file sink

**From:** WildPalms (a libkalburator consumer — Palm OS PIM sync)
**To:** libkalburator maintainers
**Date:** 2026-05-25
**Re:** what WildPalms needs in order to put Palm *memos* on the same first-class,
shape-graph footing as calendar/contacts/todo — while keeping human-readable
Markdown files on disk.

This document describes **capabilities and contracts we need**, not how to build
them. The canonical's internal representation, wire format, and naming are entirely
your call. Where we name something concrete (e.g. "document", "markdown"), treat it
as a suggestion unless we flag it as a hard requirement.

---

## 1. Context

We are the consumer described in your `docs/2026-05-24-libkalburator-canon-branch-handoff.md`.
We have adopted the `feature/canon-upgrade-convergence` branch and migrated three of
our four PIM domains onto the shape graph:

- **calendar** — our Palm backend declares `(calendar, palm)` and presents raw Palm
  wire bytes; we register `(calendar, palm) ↔ (calendar, ical)` edges on our side, and
  your `ical ↔ canon` edges carry it the rest of the way.
- **contacts** — same pattern with `(contacts, palm) ↔ (contacts, vcard4)`.
- **todo** — same pattern with `(todo, palm) ↔ (todo, ical-vtodo)`.

In every case the engine now sees the full `palm → <peer> → canon` chain with an honest
`LossProfile`, exactly as intended.

**Memo is the holdout.** Today your memo domain (`src/memo/`) exposes only a thin
canonical `(memo, text)` (`{id, body, categories, lastmodified}`) with a text
differ/merger and **no peer encodings and no edges**. Our Palm memo plugin therefore
still declares the legacy `(blob, raw)` shape and pre-transcodes Palm memo records into
**Markdown with YAML frontmatter** internally, copying the bytes verbatim through the
identity blob pipeline. That keeps memo off the shape graph and makes its palm→text loss
invisible to the engine — the exact problem the convergence was meant to solve.

We could mechanically mirror the other three (flip to `(memo, palm)`, route through
`(memo, text)`). But that would do two unwanted things:

1. It would reduce memos to plain text in the canonical, throwing away structure we want
   to keep as the document world gets richer.
2. Because your on-disk sink (`RawFilesBackend`) mirrors the *source* shape, the files
   on disk would become Palm wire bytes — losing the **human-readable Markdown files**
   that are a real WildPalms feature (our memo view reads them directly).

So instead of mirroring, we are asking for a small amount of new domain surface that
serves memos *and* generalizes cleanly.

---

## 2. What we need (the three asks)

### 2.1 A `document` domain whose canonical round-trips Markdown faithfully

We need a PIM domain for free-form documents whose **canonical can represent everything
Markdown expresses without loss** — headings, lists, emphasis, links, code (inline and
block), blockquotes. Plain-text Palm memos sit comfortably inside that.

- **Hard requirement:** Markdown is a **lossless peer** of the canonical for the content
  above. A `markdown → canon → markdown` round-trip of ordinary Markdown is byte-stable
  (modulo insignificant whitespace/normalization you document).
- **Your call:** the canonical's actual shape, richness *beyond* Markdown, and whether it
  is JSON/AST/other. "Equal-to-Markdown" fidelity is all we require; any headroom you
  choose to add (tables, footnotes, inline HTML, rich-text for future HTML/web-note
  sources) is welcome but not something we depend on. Please don't gold-plate it on our
  account — a second, richer source isn't on our roadmap yet.
- **Naming:** we suggest `document` because memos are just one kind of document and the
  domain could later serve notes/articles/web clippings. If you'd rather enrich the
  existing `memo` domain or pick another name, that's fine; we only need the capability.

### 2.2 A `(document, markdown)` peer encoding with round-trippable side metadata

We need a registered **Markdown peer encoding** in the domain that we can register our own
edges against (our Palm side — see §5), with one specific contract:

- **Hard requirement — round-trippable side metadata.** The Markdown peer must carry
  arbitrary namespaced metadata that does **not** appear in the document body and survives
  a `markdown → canon → markdown` cycle byte-for-byte, declared `Reversible` in the loss
  profile. We need this to stash Palm identity (`X-WP-PALM-RECORDID`, the Palm category
  slot, a private flag) so it round-trips without polluting the visible document.
- **Strong preference — surface it as YAML frontmatter.** A leading `---`-fenced YAML
  block at the top of the `.md` is the natural, human-friendly carrier and is what our
  existing format and reader already use. If you carry the side metadata some other way in
  the canonical that's fine, but the **on-disk Markdown should present it as YAML
  frontmatter** (this matters for §2.3 and our view).

This is the document-domain analogue of `providerExtras` in your other canons — same idea,
Markdown-flavored surface.

### 2.3 A universal Markdown-file sink backend

We need an on-disk sink that writes readable Markdown, in the same universal family as your
`RawFilesBackend` (`src/universal/`):

- Declares `(document, markdown)` (so the engine routes `palm → … → markdown` *into* it,
  rather than mirroring the source shape).
- Writes **one `.md` file per record**.
- Derives the **filename from the document title / first non-empty body line** (sanitized,
  with a stable fallback such as `document_<id>.md` for empty/degenerate bodies).
- Persists and **round-trips the §2.2 frontmatter** (so re-reading a file reconstructs the
  same record, identity intact).

Functionally this is the role `RawFilesBackend` plays for the other shapes, specialized so
the artifacts are first-class Markdown documents rather than opaque encoded blobs. If it
turns out a thin configuration of `RawFilesBackend` (extension `.md`, title-based naming)
already satisfies this, even better — we don't need a heavyweight new class, just the
behavior above.

---

## 3. Parity expectations

We expect the `document` domain to expose the same machinery the calendar/contacts/todo
canons already do, so it composes with our pipelines and UI uniformly:

- The four-kind **`LossProfile`** taxonomy (`Dropped` / `Simplified` / `Reversible` /
  `Degraded`) on its edges, including `losslessValues` where a value-dependent edge only
  degrades some values.
- A canonical **differ and merger** (your existing `TextDiffer`/`TextMerger` may well be
  the right starting point for a Markdown/text canonical).
- Registration through the same **`ShapeRegistries`** bundle / stock-shapes path, and
  reachability of the canonical via a **versioned canonical spine** so future canon
  versions are append-only.
- A **stock shapes contribution** (the analogue of `CalendarStockShapes`/`TodoStockShapes`)
  registering the canonical, the Markdown peer, and the `markdown ↔ canon` edges — memo
  currently has none, which is why nothing routes today.

No breaking changes to the converged API are implied by any of this; it should be additive,
the way the other three domains are.

---

## 4. Source you are welcome to lift

The Markdown encode/decode and file-naming logic we already run is general-purpose and, in
our view, **belongs in libkalburator**, not in a Palm plugin. Please browse and take
whatever is useful — adopt it wholesale, generalize it, or use it as a reference spec:

- `src/plugins/memo/memomarkdown.h` / `memomarkdown.cpp` — encodes a memo to Markdown with
  canonical YAML frontmatter (hash-stable output; omits default-valued fields; body ends
  in exactly one `\n`), decodes Markdown tolerantly (missing/malformed frontmatter, integer
  or string `category:`, unknown keys), and derives a human-friendly filename from the first
  body line (`filenameFor`, with a `memo_<recordId>.md` fallback).
- `src/plugins/memo/memoview.cpp` — the frontmatter parser we read files back with
  (the `^---\n(.+?)\n---\n` block, `id:`, `category:` handling), useful as a compatibility
  reference for the exact on-disk dialect we already have on users' disks.
- `src/plugins/memo/memoblobbackend.{h,cpp}` — the current per-record backend behavior
  (one collection, category slot preserved on write) for reference.

Once you ship the domain + Markdown peer + sink, **we will delete our redundant
encode/decode/sink code and depend on yours.** Treat that as the explicit goal: we want to
stop carrying this.

---

## 5. How WildPalms will consume it (informational)

So you can picture the usage shape — none of this asks anything further of you:

- Our Palm memo plugin will declare **`(document, palm)`** as its native shape and present
  raw Palm `MemoDB` wire bytes (mirroring what we already did for calendar/contacts/todo).
- On *our* side we will register `(document, palm) ↔ (document, markdown)` edges (a
  `DocumentDomainExtension`, exactly like our `CalendarDomainExtension`), reusing our Palm
  memo codec. Our edge carries an honest loss profile (palm→markdown is essentially
  lossless; markdown→palm flattens Markdown structure to plain text — `Simplified`).
- The engine then routes `palm → markdown → canon` for diffing, and the memo sync's on-disk
  peer becomes your **Markdown-file sink** (§2.3) instead of the shape-mirroring
  `RawFilesBackend`, so users get readable `.md` files with our identity frontmatter.
- Palm identity survives because of the §2.2 round-trip guarantee.

We expect to verify all of this with a `palm → canon → palm` round-trip test and a
loss-honesty test, the same way we gated each prior phase.

---

## 6. Non-goals / explicitly your call

We are **not** asking you to:

- Adopt any particular canonical representation, AST, or wire format.
- Model rich-document features beyond Markdown fidelity (do so only if it serves *your*
  roadmap).
- Keep or deprecate the existing `(memo, text)` domain — fold it into `document`, leave it,
  or retire it as you see fit; we will migrate off `(memo, text)` regardless.
- Decide whether a `(document, text)` plain-text peer exists — not something we need.
- Adopt our frontmatter key names verbatim; we only need *a* round-trippable side-metadata
  channel surfaced as frontmatter (§2.2).

---

## 7. Acceptance — our definition of done

We consider this delivered when, building against your branch, WildPalms can:

1. Register `(document, palm)` and a `(document, palm) ↔ (document, markdown)` edge, and
   `compile(palm, canon)` succeeds (i.e. the `markdown ↔ canon` edges exist and the spine
   is reachable).
2. Route a Palm memo `palm → canon → palm` and recover its **body and Palm identity**
   (record id + category slot) intact, with the canonical preserving Markdown structure.
3. Run a memo sync whose on-disk artifacts are **readable `.md` files** — one per memo,
   sensibly named, with our identity carried in YAML frontmatter — produced by your
   Markdown-file sink, and re-readable into identical records.
4. Read an honest composed `LossProfile` for the path (palm→markdown lossless;
   markdown→palm `Simplified` for dropped Markdown structure).

When that holds, we delete our redundant Markdown code (§4) and rely on yours.

---

## 8. Priority / sequencing note

This unblocks our Phase 5 (memo). It is **not** urgent relative to merging the canon
convergence branch to your `main` — we are still building against the branch and will not
ship until that merge lands and is device-verified. Treat this as the next domain to add
once the convergence work is settled. Our adoption is a small, well-understood follow-on
once the three capabilities above exist.
