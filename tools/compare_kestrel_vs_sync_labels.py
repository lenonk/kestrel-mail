#!/usr/bin/env python3
import argparse
import csv
import re
import sqlite3
from pathlib import Path

CAT_RE = re.compile(r"\[imap-category\].*gmMsgId=\s*(\d+).*labels=\s*\"([^\"]*)\".*inferred=\s*([^\s]+)")


def parse_sync_labels(log_path: Path):
    by_gm = {}
    for line in log_path.read_text(errors='ignore').splitlines():
        m = CAT_RE.search(line)
        if not m:
            continue
        gm = m.group(1).strip()
        labels = m.group(2).strip()
        inferred = m.group(3).strip()
        by_gm[gm] = {"labels": labels, "inferred": inferred}
    return by_gm


def category_from_labels(raw: str):
    s = (raw or '').lower()
    for k in ["/categories/primary", "/categories/promotion", "/categories/social", "/categories/purchase", "/categories/update", "/categories/forum"]:
        if k in s:
            return k
    return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kestrel-db", required=True)
    ap.add_argument("--debug-log", required=True)
    ap.add_argument("--out", default="tmp/mapping-compare/kestrel_vs_sync_labels.csv")
    args = ap.parse_args()

    sync = parse_sync_labels(Path(args.debug_log))

    con = sqlite3.connect(args.kestrel_db)
    cur = con.cursor()
    rows = cur.execute(
        """
        SELECT m.gm_msg_id, m.sender, m.subject, m.received_at,
               group_concat(DISTINCT mf.folder) as folders
        FROM messages m
        JOIN message_folder_map mf ON mf.message_id = m.id
        WHERE m.gm_msg_id IS NOT NULL AND length(trim(m.gm_msg_id)) > 0
        GROUP BY m.id
        ORDER BY m.received_at DESC
        LIMIT 400
        """
    ).fetchall()
    con.close()

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    compared = 0
    mismatch = 0

    with out_path.open('w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(["gm_msg_id", "sender", "subject", "received_at", "kestrel_folders", "sync_labels", "sync_category", "kestrel_has_sync_category", "match"])
        for gm, sender, subject, received_at, folders in rows:
            gm = (gm or '').strip()
            if gm not in sync:
                continue
            compared += 1
            kfolders = sorted([x.strip() for x in (folders or '').split(',') if x.strip()])
            sync_labels = sync[gm]["labels"]
            sync_cat = category_from_labels(sync_labels)
            has = any(sync_cat in x.lower() for x in kfolders) if sync_cat else True
            match = has
            if not match:
                mismatch += 1
            w.writerow([gm, sender or '', subject or '', received_at or '', " | ".join(kfolders), sync_labels, sync_cat, str(has).lower(), str(match).lower()])

    print(f"sync_gm_entries={len(sync)} compared={compared} mismatch={mismatch}")
    print(f"wrote: {out_path}")


if __name__ == '__main__':
    main()
