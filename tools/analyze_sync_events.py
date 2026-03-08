#!/usr/bin/env python3
import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path


def pct(n, d):
    return (100.0 * n / d) if d else 0.0


def load_events(path: Path):
    events = []
    if not path.exists():
        return events
    with path.open("r", encoding="utf-8") as f:
        for line_no, line in enumerate(f, start=1):
            s = line.strip()
            if not s:
                continue
            try:
                obj = json.loads(s)
            except Exception:
                continue
            obj["_line"] = line_no
            events.append(obj)
    return events


def is_category_warning_scope(folder: str) -> bool:
    f = (folder or "").strip().lower()
    return f == "inbox" or "/categories/" in f


def main():
    ap = argparse.ArgumentParser(description="Analyze Kestrel sync-events.jsonl for failures and category instability")
    ap.add_argument("--log", default=str(Path.home() / ".local/share/kestrel-mail/logs/sync-events.jsonl"))
    ap.add_argument("--tail", type=int, default=400, help="Only analyze last N events")
    ap.add_argument("--warn-miss-rate", type=float, default=25.0, help="Warn when category miss rate exceeds this percentage")
    ap.add_argument("--warn-delta", type=float, default=15.0, help="Warn when miss rate jumps by this many points between consecutive category summaries")
    ap.add_argument("--warn-all-folders", action="store_true", help="Warn for all folders (default warns only for INBOX/category folders)")
    args = ap.parse_args()

    events = load_events(Path(args.log))
    if args.tail > 0:
        events = events[-args.tail:]

    if not events:
        print("no_events=1")
        return

    by_event = Counter(e.get("event", "") for e in events)

    sync_finish = [e for e in events if e.get("event") == "sync_finish"]
    ok_count = sum(1 for e in sync_finish if bool(e.get("ok")) is True)
    fail_count = len(sync_finish) - ok_count
    cancelled_count = sum(1 for e in sync_finish if str(e.get("reason", "")).lower() == "cancelled")

    scope_counts = Counter(str(e.get("scope", "")) for e in sync_finish)
    scope_fail = Counter(str(e.get("scope", "")) for e in sync_finish if not bool(e.get("ok")))

    category = [e for e in events if e.get("event") == "imap_category_summary"]
    folder_stats = defaultdict(lambda: {"fetched": 0, "misses": 0, "samples": 0, "rates": []})
    spikes = []

    prev_rate_by_folder = {}
    for e in category:
        folder = str(e.get("folder", ""))
        fetched = int(e.get("inboxFetched", 0) or 0)
        misses = int(e.get("inboxCategoryMisses", 0) or 0)
        rate = pct(misses, fetched)
        st = folder_stats[folder]
        st["fetched"] += fetched
        st["misses"] += misses
        st["samples"] += 1
        st["rates"].append(rate)

        prev = prev_rate_by_folder.get(folder)
        warn_scope = args.warn_all_folders or is_category_warning_scope(folder)
        if warn_scope and prev is not None and (rate - prev) >= args.warn_delta:
            spikes.append((folder, prev, rate, e.get("_line")))
        prev_rate_by_folder[folder] = rate

    print(f"events_total={len(events)}")
    print(f"event_sync_start={by_event.get('sync_start', 0)}")
    print(f"event_sync_finish={by_event.get('sync_finish', 0)}")
    print(f"event_imap_category_summary={by_event.get('imap_category_summary', 0)}")
    print(f"sync_finish_ok={ok_count}")
    print(f"sync_finish_fail={fail_count}")
    print(f"sync_finish_cancelled={cancelled_count}")

    for scope in sorted(scope_counts):
        total = scope_counts[scope]
        failed = scope_fail.get(scope, 0)
        print(f"scope_{scope}_total={total}")
        print(f"scope_{scope}_fail={failed}")

    instability_warnings = 0
    for folder in sorted(folder_stats):
        st = folder_stats[folder]
        rate = pct(st["misses"], st["fetched"])
        warn_scope = args.warn_all_folders or is_category_warning_scope(folder)
        print(f"category_folder={folder} samples={st['samples']} fetched={st['fetched']} misses={st['misses']} miss_rate={rate:.2f}")
        if rate >= args.warn_miss_rate:
            if warn_scope:
                instability_warnings += 1
                print(f"WARN high_miss_rate folder={folder} miss_rate={rate:.2f} threshold={args.warn_miss_rate:.2f}")
            else:
                print(f"INFO high_miss_rate_ignored folder={folder} miss_rate={rate:.2f} reason=non-category-scope")

    for folder, prev, now, line_no in spikes:
        instability_warnings += 1
        print(f"WARN miss_rate_spike folder={folder} prev={prev:.2f} now={now:.2f} delta={now-prev:.2f} line={line_no}")

    print(f"instability_warnings={instability_warnings}")


if __name__ == "__main__":
    main()
