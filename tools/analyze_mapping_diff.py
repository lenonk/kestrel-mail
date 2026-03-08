#!/usr/bin/env python3
import argparse
import csv
from collections import Counter
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description="Summarize mapping_diff.csv to diagnose only_em/only_kestrel gaps")
    ap.add_argument("--diff", default="tmp/mapping-compare/mapping_diff.csv")
    ap.add_argument("--top", type=int, default=15)
    args = ap.parse_args()

    path = Path(args.diff)
    if not path.exists():
        print(f"error=missing_diff path={path}")
        return 2

    only_em_senders = Counter()
    only_em_subjects = Counter()
    only_em_sender_subject = Counter()

    only_k_senders = Counter()
    only_k_subjects = Counter()

    folder_mismatch_subjects = Counter()

    total = 0
    only_em = 0
    only_k = 0
    mismatch = 0

    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            total += 1
            typ = (row.get("type") or "").strip()
            sender = (row.get("sender") or "").strip()
            subject = (row.get("subject") or "").strip()
            key = f"{sender} || {subject}".strip()

            if typ == "only_em":
                only_em += 1
                only_em_senders[sender] += 1
                only_em_subjects[subject] += 1
                only_em_sender_subject[key] += 1
            elif typ == "only_kestrel":
                only_k += 1
                only_k_senders[sender] += 1
                only_k_subjects[subject] += 1
            elif typ == "folder_mismatch":
                mismatch += 1
                folder_mismatch_subjects[subject] += 1

    print(f"rows_total={total}")
    print(f"only_em={only_em}")
    print(f"only_kestrel={only_k}")
    print(f"folder_mismatch={mismatch}")

    def dump_counter(name, c):
        print(f"\n[{name}]")
        for item, n in c.most_common(args.top):
            label = item if item else "<empty>"
            print(f"{n}\t{label}")

    dump_counter("only_em.top_senders", only_em_senders)
    dump_counter("only_em.top_subjects", only_em_subjects)
    dump_counter("only_em.top_sender_subject", only_em_sender_subject)
    dump_counter("only_kestrel.top_senders", only_k_senders)
    dump_counter("folder_mismatch.top_subjects", folder_mismatch_subjects)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
