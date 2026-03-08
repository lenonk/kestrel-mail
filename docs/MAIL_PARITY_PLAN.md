# Kestrel Mail Parity Plan (eM Client / Gmail-equivalent behavior)

## Goal
Achieve stable message/folder/category behavior that matches Gmail web behavior as observed in eM Client.

## Acceptance Criteria
1. **Mapping parity oracle** exists and is runnable in one command.
2. For a defined comparison window (default: recent 300 messages), mapping diffs trend to zero or known/accepted exceptions.
3. Category/Primary behavior remains stable across repeated sync cycles (no oscillation after sync phases).

## Current Oracle
- `tools/compare_latest.sh`
- `tools/compare_mappings.py`
- Output:
  - `tmp/mapping-compare/kestrel_map.csv`
  - `tmp/mapping-compare/em_map.csv`
  - `tmp/mapping-compare/mapping_diff.csv`

## Immediate Stability Rules
1. Never prune category folders using UID sets from a different folder namespace (e.g., All Mail UIDs).
2. For INBOX messages, run category fallback when labels exist but category label is absent (e.g., `\Important` only).
3. Add and keep evidence logs for classification decisions:
   - `[imap-category]`
   - `[imap-category-miss]`
   - `[imap-category-summary]`

## Architecture Direction (copy proven patterns)

### 1) Storage Separation (index vs payload)
- Keep list/index fields in a fast table (sender, subject, receivedAt, flags, message keys).
- Keep body/html payload separate.
- Never let body hydration logic alter category mapping logic.

### 2) Folder Metadata Model
- Dedicated folder table with stable IDs and attributes (special-use flags, display metadata, sync metadata).
- Avoid overloading message rows with folder semantics.

### 3) Authoritative Mapping Table
- `message_folder_map(message_id, folder_id, source, confidence, observed_at)`
- `source`: `imap-label` | `imap-inferred` | `local-rule`
- `confidence`: integer score for safe reconciliation.

### 4) Reconciliation Policy
- Reconcile only within same namespace semantics.
- Deletion/prune operations must log reason code and row counts.
- High-confidence mappings require high-confidence evidence for removal.

### 5) Feature Growth (eM-style)
- conversation/thread support
- queued operations tables (download/delete/reclassify)
- durable unread/category counters
- search index split (mail/attachments)
- per-account sync state + checkpoints
- user option: "Download all messages for offline use" (deferred; not enabled by default)

## Implementation Phases

### Phase A (now): Reliability over breadth
- [x] Add detailed category diagnostics
- [x] Fix category fallback skip case (`\Important` without category)
- [x] Remove invalid category prune path based on All Mail UID sets
- [ ] Improve comparator matching quality (message-id/gm-msgid when available)
- [ ] Add machine-readable sync event log (JSON lines)

### Phase B: Data-model hardening
- [ ] Introduce explicit mapping provenance/confidence
- [ ] Add guarded prune API with namespace checks
- [ ] Add invariant tests for category membership stability

### Phase C: eM-feature trajectory
- [ ] Expand sync/state tables for operational durability
- [ ] Add richer parity suite by folder class and message age bands
- [ ] Migrate additional UX features once sync correctness is consistently green

## Operational Workflow
1. Sync.
2. Run `tools/compare_latest.sh`.
3. Inspect `mapping_diff.csv`.
4. If mismatch spike occurs, inspect category logs around phase boundaries.
5. Fix with smallest possible patch and re-run.

## Notes
- "All Mail" should be treated as a presentation/convenience view unless explicit server semantics are required.
- Category correctness should be derived from Gmail label evidence and robust fallback, not cross-folder UID assumptions.
