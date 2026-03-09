# TODO

Consolidated action list for Kestrel (migrated from scattered notes/memory/docs).

## Product / UX

- [ ] Add user-facing settings to enable/disable Gmail category tabs (Purchases, Updates, Forums), default hidden.
- [ ] Add user setting to toggle hover-expand behavior in the content pane (keep feature, make configurable).
- [ ] Add user option: **Download all messages for offline use** (deferred; not default behavior).
- [ ] Ensure message list pane never changes scroll position during updates (verify/follow up after current fix in `MessageListPane.qml`).
- [ ] On folder switch (new folder selection), auto-scroll message list to top.
- [ ] Tags section should auto-refresh on startup: ensure tag data appears without manual collapse/expand, and default Tags group expanded state remains correct.
- [ ] Window close via X no longer exits app reliably (regression after shutdown/crash fix) — restore normal close behavior without reintroducing thread-destroy crash.
- [ ] Move MessageContentPane heavy processing from QML to C++ backend (first targets: sanitizeRenderHtml, tracking pixel/link normalization, darkenHtml injection pipeline).
- [ ] Implement conversation/threaded message view (high priority; start soon).
- [ ] Replace hardcoded Gmail category handling with DB-discovered categories (discover from message labels, persist category set, and read categories from DB everywhere UI/logic iterates them).

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

## Notes

- Gmail categories are Kestrel-specific and should not be treated as Mailspring parity acceptance criteria.
