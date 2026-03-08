#!/usr/bin/env python3
import argparse
import csv
import sqlite3
from pathlib import Path


def normalize_mid(s: str) -> str:
    x = (s or "").strip().lower()
    if x.startswith("<") and x.endswith(">") and len(x) > 2:
        x = x[1:-1].strip()
    return x


def load_only_em(diff_csv: Path):
    rows = []
    with diff_csv.open("r", encoding="utf-8", newline="") as f:
        r = csv.DictReader(f)
        for row in r:
            if (row.get("type") or "").strip() == "only_em":
                rows.append(row)
    return rows


def load_em_rows(mail_index: Path):
    con = sqlite3.connect(str(mail_index))
    cur = con.cursor()
    q = """
    SELECT COALESCE((SELECT CASE WHEN ma.displayName IS NOT NULL AND ma.displayName != ''
                                 THEN ma.displayName || ' <' || ma.address || '>'
                                 ELSE ma.address END
                     FROM MailAddresses ma
                     WHERE ma.parentId = m.id AND ma.type = 1
                     ORDER BY ma.position ASC LIMIT 1), '') AS sender,
           COALESCE(m.subject, '') AS subject,
           COALESCE(m.messageId, '') AS message_id
    FROM MailItems m
    """
    out = {}
    for sender, subject, mid in cur.execute(q):
        key = (sender.strip(), subject.strip())
        out.setdefault(key, set()).add(normalize_mid(mid))
    con.close()
    return out


def main():
    ap = argparse.ArgumentParser(description="Probe whether only_em rows already exist in Kestrel by Message-ID")
    ap.add_argument("--diff", default="tmp/mapping-compare/mapping_diff.csv")
    ap.add_argument("--kestrel-db", default=str(Path.home() / ".local/share/kestrel-mail/mail.db"))
    ap.add_argument("--em-mail-index", required=True)
    ap.add_argument("--top", type=int, default=30)
    args = ap.parse_args()

    only_em = load_only_em(Path(args.diff))
    em_lookup = load_em_rows(Path(args.em_mail_index))

    con = sqlite3.connect(args.kestrel_db)
    cur = con.cursor()

    total = 0
    found_by_mid = 0
    found_with_edges = 0
    missing_mid = 0

    detail = []

    for row in only_em[: args.top]:
        sender = (row.get("sender") or "").strip()
        subject = (row.get("subject") or "").strip()
        mids = [m for m in em_lookup.get((sender, subject), set()) if m]
        total += 1

        if not mids:
            missing_mid += 1
            detail.append((sender, subject, "no_message_id_in_em", 0, 0))
            continue

        placeholders = ",".join("?" for _ in mids)
        q = f"""
            SELECT m.id,
                   lower(trim(COALESCE(m.message_id_header,''))) as mid,
                   (SELECT COUNT(*) FROM message_folder_map mf WHERE mf.message_id = m.id) as edge_count
            FROM messages m
            WHERE lower(trim(COALESCE(m.message_id_header,''))) IN ({placeholders})
            LIMIT 1
        """
        hit = cur.execute(q, mids).fetchone()
        if hit:
            found_by_mid += 1
            edge_count = int(hit[2] or 0)
            if edge_count > 0:
                found_with_edges += 1
            detail.append((sender, subject, "found", 1, edge_count))
        else:
            detail.append((sender, subject, "missing_in_kestrel", 0, 0))

    con.close()

    print(f"sampled_only_em={total}")
    print(f"found_by_message_id={found_by_mid}")
    print(f"found_with_folder_edges={found_with_edges}")
    print(f"em_rows_without_message_id={missing_mid}")

    print("\n[top_detail]")
    for sender, subject, status, hit, edges in detail:
        print(f"status={status}\thit={hit}\tedges={edges}\tsender={sender}\tsubject={subject}")


if __name__ == "__main__":
    main()
