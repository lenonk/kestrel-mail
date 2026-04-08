# TODO

Consolidated action list for Kestrel (migrated from scattered notes/memory/docs).

## Product / UX

- [ ] Add user-facing settings to enable/disable Gmail category tabs (Purchases, Updates, Forums), default hidden.
- [ ] Add user option: **Download all messages for offline use** (deferred; not default behavior).
- [ ] Replace hardcoded Gmail category handling with DB-discovered categories (discover from message labels, persist category set, and read categories from DB everywhere UI/logic iterates them).
- [ ] Make Local Folders an option, off by default (visual clutter for most users).
- [ ] Make Favorites section an option the user can turn off.
- [ ] Calendar month view: fetch next/previous month's events on the fly when navigating.

## Sync / Parity / Correctness

- [ ] Revisit snippet extraction/cleanup pipeline:
  - fix malformed/garbled snippets,
  - strip forwarded-message contamination,
  - improve encoding handling,
  - keep one-line preview stable/human-readable.
- [ ] Validate Gmail full-fetch behavior: confirm whether Trash populates when syncing from only All Mail.
- [ ] Improve comparator matching quality (prefer message-id / gm-msgid where available).
- [ ] Add machine-readable sync event log (JSONL).
- [ ] Introduce explicit mapping provenance/confidence.
- [ ] Add guarded prune API with namespace checks.
- [ ] Add invariant tests for category membership stability.
- [ ] Expand sync/state tables for operational durability.
- [ ] Add richer parity suite by folder class and message age bands.
- [ ] Continue UX feature migration only after sync correctness is consistently green.
- [ ] Finalize incremental sync posture across providers:
  - [x] CONDSTORE `CHANGEDSINCE`/modseq-aware fetch paths implemented.
  - [ ] QRESYNC/VANISHED support where the server actually advertises QRESYNC capability (Gmail does not).
  - [x] Full `UID SEARCH ALL` avoided in authoritative no-change windows.
- [ ] Reduce no-op sync churn: eliminate redundant same-connection `SELECT` cycles for unchanged folders.

## Notes

- Gmail categories are Kestrel-specific and should not be treated as Mailspring parity acceptance criteria.
