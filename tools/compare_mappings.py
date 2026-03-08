#!/usr/bin/env python3
import argparse
import csv
import datetime as dt
import sqlite3
from collections import defaultdict
from pathlib import Path


def normalize_label(label: str) -> str:
    s = (label or '').strip()
    l = s.lower()
    if l in ('inbox', '\\inbox', '/inbox', '[gmail]/inbox', '[google mail]/inbox'):
        return 'INBOX'
    if l in ('important', '\\important', '/important', '[gmail]/important', '[google mail]/important'):
        return 'IMPORTANT'
    if l in ('\\sent', '/sent', 'sent', '[gmail]/sent mail', '/[gmail]/sent mail', '[google mail]/sent mail'):
        return '[Gmail]/Sent Mail'
    if l in ('/[gmail]/all mail', '[gmail]/all mail', '[gmail]/allmail', '[google mail]/all mail'):
        return '[Gmail]/All Mail'

    # Canonicalize Gmail category aliases/plurals.
    if '/categories/promotion' in l or '/categories/promotions' in l:
        return '[Gmail]/Categories/Promotions'
    if '/categories/social' in l:
        return '[Gmail]/Categories/Social'
    if '/categories/update' in l or '/categories/updates' in l:
        return '[Gmail]/Categories/Updates'
    if '/categories/purchase' in l or '/categories/purchases' in l:
        return '[Gmail]/Categories/Purchases'
    if '/categories/forum' in l or '/categories/forums' in l:
        return '[Gmail]/Categories/Forums'
    if '/categories/primary' in l:
        return '[Gmail]/Categories/Primary'

    if l.startswith('/[gmail]/'):
        return '[Gmail]/' + s.split('/[Gmail]/', 1)[1] if '/[Gmail]/' in s else s[1:]
    return s


def em_ticks_to_iso(ticks: int) -> str:
    # .NET ticks: 100ns since 0001-01-01
    unix_ticks = 621355968000000000
    try:
        sec = (ticks - unix_ticks) / 10_000_000
        d = dt.datetime.fromtimestamp(sec, tz=dt.timezone.utc)
        return d.replace(microsecond=0).isoformat().replace('+00:00', 'Z')
    except Exception:
        return ""


def norm(s: str) -> str:
    return " ".join((s or "").strip().lower().split())


def normalize_message_id(mid: str) -> str:
    s = (mid or "").strip().lower()
    if not s:
        return ""
    # RFC 5322 message-id often appears in angle brackets; normalize for robust pairing.
    if s.startswith("<") and s.endswith(">") and len(s) > 2:
        s = s[1:-1].strip()
    return s


def sender_email(sender: str) -> str:
    s = (sender or "").strip().lower()
    if "<" in s and ">" in s:
        start = s.find("<") + 1
        end = s.find(">", start)
        if end > start:
            return s[start:end].strip()
    return s


def key(sender: str, subject: str, received_iso: str, mode: str) -> str:
    if mode in ("subject-time", "fuzzy-subject-time"):
        return f"{norm(subject)}|{received_iso}"
    return f"{norm(sender)}|{norm(subject)}|{received_iso}"


def parse_iso_utc(s: str):
    if not s:
        return None
    try:
        if s.endswith('Z'):
            return dt.datetime.fromisoformat(s.replace('Z', '+00:00'))
        return dt.datetime.fromisoformat(s)
    except Exception:
        return None


def load_kestrel(db_path: Path, limit: int):
    con = sqlite3.connect(str(db_path))
    cur = con.cursor()

    cols = {r[1] for r in cur.execute("PRAGMA table_info(messages)")}
    has_mid = 'message_id_header' in cols
    has_gm = 'gm_msg_id' in cols
    tables = {r[0] for r in cur.execute("SELECT name FROM sqlite_master WHERE type='table'")}
    has_labels = 'message_labels' in tables
    has_map = 'message_folder_map' in tables

    mid_col = "m.message_id_header" if has_mid else "''"
    gm_col = "m.gm_msg_id" if has_gm else "''"
    label_col = "group_concat(DISTINCT ml.label) AS labels" if has_labels else "'' AS labels"
    label_join = "LEFT JOIN message_labels ml ON ml.message_id = m.id" if has_labels else ""

    if has_map:
        folder_join = "JOIN message_folder_map mf ON mf.message_id = m.id"
    else:
        con.close()
        return []

    limit_clause = "LIMIT ?" if limit and limit > 0 else ""
    q = f"""
    SELECT m.sender, m.subject, m.received_at, {mid_col} AS message_id, {gm_col} AS gm_msg_id,
           group_concat(DISTINCT mf.folder) AS folders,
           {label_col}
    FROM messages m
    {folder_join}
    {label_join}
    GROUP BY m.id
    ORDER BY m.received_at DESC, m.id DESC
    {limit_clause}
    """
    rows = []
    params = (limit,) if (limit and limit > 0) else ()
    for sender, subject, received_at, message_id, gm_msg_id, folders, labels in cur.execute(q, params):
        base_folders = [normalize_label(f.strip()) for f in (folders or "").split(',') if f.strip()]
        label_list = [normalize_label(f.strip()) for f in (labels or "").split(',') if f.strip()]

        # message_labels is authoritative for category labels and Important signal when present.
        non_category = [f for f in base_folders if '/Categories/' not in f]
        category_from_labels = [f for f in label_list if '/Categories/' in f]
        important_from_labels = [f for f in label_list if f == 'IMPORTANT']
        merged = non_category + category_from_labels + important_from_labels
        folder_set = sorted(set(merged)) if (category_from_labels or important_from_labels) else sorted(set(base_folders))

        rows.append({
            "sender": sender or "",
            "subject": subject or "",
            "received_at": (received_at or "").replace("+00:00", "Z"),
            "message_id": (message_id or "").strip(),
            "gm_msg_id": (gm_msg_id or "").strip(),
            "folders": folder_set,
        })
    con.close()
    return rows


def load_em(mail_index: Path, folders_db: Path, limit: int):
    con = sqlite3.connect(str(mail_index))
    cur = con.cursor()
    cur.execute("ATTACH DATABASE ? AS fdb", (str(folders_db),))

    limit_clause = "LIMIT ?" if limit and limit > 0 else ""
    q = f"""
    SELECT m.id,
           COALESCE((SELECT CASE WHEN ma.displayName IS NOT NULL AND ma.displayName != ''
                                 THEN ma.displayName || ' <' || ma.address || '>'
                                 ELSE ma.address END
                     FROM MailAddresses ma
                     WHERE ma.parentId = m.id AND ma.type = 1
                     ORDER BY ma.position ASC LIMIT 1), '') AS sender,
           COALESCE(m.subject, '') AS subject,
           COALESCE(m.messageId, '') AS message_id,
           m.receivedDate,
           COALESCE(f.path, '') AS folder_path,
           COALESCE((SELECT group_concat(mc.categoryName, '|')
                     FROM MailCategoryNames mc
                     WHERE mc.id = m.id), '') AS categories
    FROM MailItems m
    LEFT JOIN fdb.Folders f ON f.id = m.folder
    ORDER BY m.receivedDate DESC, m.id DESC
    {limit_clause}
    """

    out = []
    params = (limit,) if (limit and limit > 0) else ()
    for _id, sender, subject, message_id, received_ticks, folder_path, categories in cur.execute(q, params):
        received_iso = em_ticks_to_iso(int(received_ticks)) if received_ticks else ""
        labels = []
        if folder_path:
            labels.append(normalize_label(folder_path))
        if categories:
            labels.extend([normalize_label(c.strip()) for c in categories.split('|') if c.strip()])
        out.append({
            "sender": sender,
            "subject": subject,
            "received_at": received_iso,
            "message_id": (message_id or "").strip(),
            "gm_msg_id": "",
            "folders": sorted(set(labels)),
        })

    con.close()
    return out


def to_map(rows, mode):
    m = {}
    for r in rows:
        m[key(r['sender'], r['subject'], r['received_at'], mode)] = r
    return m


def fuzzy_pair(k_rows, e_rows, tolerance_seconds=600):
    """Pair by Message-ID first, then sender+subject+nearest timestamp, then subject+nearest timestamp."""
    pairs = []
    used_k = set()
    used_e = set()
    by_mid = 0
    by_sender_subject_time = 0
    by_subject_time = 0

    # 1) Strong match: RFC Message-ID (normalized) exact.
    e_by_mid = defaultdict(list)
    for j, r in enumerate(e_rows):
        mid = normalize_message_id(r.get('message_id'))
        if mid:
            e_by_mid[mid].append(j)

    for i, r in enumerate(k_rows):
        mid = normalize_message_id(r.get('message_id'))
        if not mid:
            continue
        cands = e_by_mid.get(mid, [])
        for j in cands:
            if j in used_e:
                continue
            pairs.append((i, j))
            used_k.add(i)
            used_e.add(j)
            by_mid += 1
            break

    # 2) Medium-strong fallback: sender-email + subject + nearest timestamp.
    k_buckets = defaultdict(list)
    e_buckets = defaultdict(list)

    for i, r in enumerate(k_rows):
        if i in used_k:
            continue
        bucket_key = (sender_email(r.get('sender', '')), norm(r.get('subject', '')))
        k_buckets[bucket_key].append((i, parse_iso_utc(r.get('received_at', ''))))
    for i, r in enumerate(e_rows):
        if i in used_e:
            continue
        bucket_key = (sender_email(r.get('sender', '')), norm(r.get('subject', '')))
        e_buckets[bucket_key].append((i, parse_iso_utc(r.get('received_at', ''))))

    for bucket, k_items in k_buckets.items():
        e_items = e_buckets.get(bucket, [])
        if not e_items:
            continue
        for ki, kdt in sorted(k_items, key=lambda t: (t[1] or dt.datetime.min.replace(tzinfo=dt.timezone.utc))):
            best = None
            best_delta = None
            for ei, edt in e_items:
                if ei in used_e or kdt is None or edt is None:
                    continue
                delta = abs((kdt - edt).total_seconds())
                if delta <= tolerance_seconds and (best_delta is None or delta < best_delta):
                    best = ei
                    best_delta = delta
            if best is not None:
                pairs.append((ki, best))
                used_k.add(ki)
                used_e.add(best)
                by_sender_subject_time += 1

    # 3) Broad fallback: subject + nearest timestamp.
    k_by_subject = defaultdict(list)
    e_by_subject = defaultdict(list)

    for i, r in enumerate(k_rows):
        if i in used_k:
            continue
        k_by_subject[norm(r['subject'])].append((i, parse_iso_utc(r['received_at'])))
    for i, r in enumerate(e_rows):
        if i in used_e:
            continue
        e_by_subject[norm(r['subject'])].append((i, parse_iso_utc(r['received_at'])))

    for subj, k_items in k_by_subject.items():
        e_items = e_by_subject.get(subj, [])
        if not e_items:
            continue
        for ki, kdt in sorted(k_items, key=lambda t: (t[1] or dt.datetime.min.replace(tzinfo=dt.timezone.utc))):
            best = None
            best_delta = None
            for ei, edt in e_items:
                if ei in used_e or kdt is None or edt is None:
                    continue
                delta = abs((kdt - edt).total_seconds())
                if delta <= tolerance_seconds and (best_delta is None or delta < best_delta):
                    best = ei
                    best_delta = delta
            if best is not None:
                pairs.append((ki, best))
                used_k.add(ki)
                used_e.add(best)
                by_subject_time += 1

    return pairs, {
        "by_mid": by_mid,
        "by_sender_subject_time": by_sender_subject_time,
        "by_subject_time": by_subject_time,
    }


def filter_rows_to_aligned_window(k_rows, e_rows, window_hours: int):
    stats = {
        "k_pre": len(k_rows),
        "e_pre": len(e_rows),
        "k_invalid_ts": 0,
        "e_invalid_ts": 0,
    }

    k_times = [parse_iso_utc(r.get('received_at', '')) for r in k_rows]
    e_times = [parse_iso_utc(r.get('received_at', '')) for r in e_rows]
    stats["k_invalid_ts"] = sum(1 for t in k_times if t is None)
    stats["e_invalid_ts"] = sum(1 for t in e_times if t is None)
    k_times = [t for t in k_times if t is not None]
    e_times = [t for t in e_times if t is not None]
    if not k_times or not e_times:
        stats["k_post"] = len(k_rows)
        stats["e_post"] = len(e_rows)
        return k_rows, e_rows, None, stats

    # Hard cap: never compare rows later than the newest eM message timestamp.
    em_latest = max(e_times)

    # For windowed comparisons, anchor to the older of both newest timestamps.
    anchor = min(max(k_times), em_latest)
    threshold = anchor - dt.timedelta(hours=window_hours) if window_hours and window_hours > 0 else None

    def keep(rows):
        out = []
        for r in rows:
            ts = parse_iso_utc(r.get('received_at', ''))
            if ts is None:
                continue
            if ts > em_latest:
                continue
            if threshold is not None and not (threshold <= ts <= anchor):
                continue
            out.append(r)
        return out

    k_kept = keep(k_rows)
    e_kept = keep(e_rows)
    stats["k_post"] = len(k_kept)
    stats["e_post"] = len(e_kept)
    stats["em_latest"] = em_latest.replace(microsecond=0).isoformat().replace('+00:00', 'Z')
    return k_kept, e_kept, anchor.replace(microsecond=0).isoformat().replace('+00:00', 'Z'), stats


def write_csv(path: Path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(["sender", "subject", "received_at", "folders"])
        for r in rows:
            w.writerow([r['sender'], r['subject'], r['received_at'], " | ".join(r['folders'])])


def main():
    ap = argparse.ArgumentParser(description="Compare Kestrel vs eM Client message->folder mappings")
    ap.add_argument("--kestrel-db", required=True)
    ap.add_argument("--em-mail-index", required=True)
    ap.add_argument("--em-folders-db", required=True)
    ap.add_argument("--limit", type=int, default=300, help="Max rows per source before windowing (0 = no limit)")
    ap.add_argument("--out-dir", default="tmp/mapping-compare")
    ap.add_argument("--match-mode", choices=["sender-subject-time", "subject-time", "fuzzy-subject-time"], default="fuzzy-subject-time")
    ap.add_argument("--window-hours", type=int, default=72,
                    help="Aligned recency window (hours). 0 disables window alignment.")
    args = ap.parse_args()

    kres = load_kestrel(Path(args.kestrel_db), args.limit)
    emres = load_em(Path(args.em_mail_index), Path(args.em_folders_db), args.limit)
    kres, emres, anchor_iso, window_stats = filter_rows_to_aligned_window(kres, emres, args.window_hours)

    k_with_mid = sum(1 for r in kres if (r.get('message_id') or '').strip())
    e_with_mid = sum(1 for r in emres if (r.get('message_id') or '').strip())

    out_dir = Path(args.out_dir)
    write_csv(out_dir / "kestrel_map.csv", kres)
    write_csv(out_dir / "em_map.csv", emres)

    primary_mismatch_rows = []

    if args.match_mode == "fuzzy-subject-time":
        pairs, pair_stats = fuzzy_pair(kres, emres)
        matched_k = {i for i, _ in pairs}
        matched_e = {j for _, j in pairs}

        only_k_rows = [kres[i] for i in range(len(kres)) if i not in matched_k]
        only_e_rows = [emres[i] for i in range(len(emres)) if i not in matched_e]

        diffs = []
        primary_signal_mismatch = 0
        for i, j in pairs:
            kr = kres[i]
            er = emres[j]
            kf = set(kr['folders'])
            ef = set(er['folders'])
            if kf != ef:
                diffs.append((kr, er, sorted(kf), sorted(ef)))

            k_has_primary = any('/Categories/Primary' in x for x in kf)
            # eM currently exposes mostly \Inbox in this dataset; treat inbox as primary signal.
            e_has_primary_signal = ('INBOX' in ef)
            if k_has_primary != e_has_primary_signal:
                primary_signal_mismatch += 1
                primary_mismatch_rows.append({
                    'sender': kr['sender'],
                    'subject': kr['subject'],
                    'received_at': kr['received_at'],
                    'kestrel_has_primary': k_has_primary,
                    'em_has_primary_signal': e_has_primary_signal,
                    'kestrel_folders': sorted(kf),
                    'em_folders': sorted(ef),
                    'message_id': kr.get('message_id', '')
                })

        common_count = len(pairs)
        only_k_count = len(only_k_rows)
        only_e_count = len(only_e_rows)
    else:
        pair_stats = {"by_mid": 0, "by_sender_subject_time": 0, "by_subject_time": 0}
        primary_signal_mismatch = 0
        km = to_map(kres, args.match_mode)
        em = to_map(emres, args.match_mode)

        only_k = sorted(set(km.keys()) - set(em.keys()))
        only_e = sorted(set(em.keys()) - set(km.keys()))
        common = sorted(set(km.keys()) & set(em.keys()))

        diffs = []
        for k in common:
            kf = set(km[k]['folders'])
            ef = set(em[k]['folders'])
            if kf != ef:
                diffs.append((k, sorted(kf), sorted(ef)))

        common_count = len(common)
        only_k_count = len(only_k)
        only_e_count = len(only_e)

    with (out_dir / "mapping_diff.csv").open('w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(["type", "sender", "subject", "received_at", "kestrel_folders", "em_folders"])

        if args.match_mode == "fuzzy-subject-time":
            for r in only_k_rows:
                w.writerow(["only_kestrel", r['sender'], r['subject'], r['received_at'], " | ".join(r['folders']), ""])
            for r in only_e_rows:
                w.writerow(["only_em", r['sender'], r['subject'], r['received_at'], "", " | ".join(r['folders'])])
            for kr, er, kf, ef in diffs:
                w.writerow(["folder_mismatch", kr['sender'], kr['subject'], kr['received_at'], " | ".join(kf), " | ".join(ef)])
        else:
            for k in only_k:
                r = km[k]
                w.writerow(["only_kestrel", r['sender'], r['subject'], r['received_at'], " | ".join(r['folders']), ""])
            for k in only_e:
                r = em[k]
                w.writerow(["only_em", r['sender'], r['subject'], r['received_at'], "", " | ".join(r['folders'])])
            for k, kf, ef in diffs:
                parts = k.split('|')
                if len(parts) == 3:
                    sender, subject, received = parts
                else:
                    sender, subject, received = "", parts[0], parts[1] if len(parts) > 1 else ""
                w.writerow(["folder_mismatch", sender, subject, received, " | ".join(kf), " | ".join(ef)])

    with (out_dir / "primary_signal_mismatch.csv").open('w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(["sender", "subject", "received_at", "message_id", "kestrel_has_primary", "em_has_primary_signal", "kestrel_folders", "em_folders"])
        for r in primary_mismatch_rows:
            w.writerow([
                r['sender'], r['subject'], r['received_at'], r['message_id'],
                str(r['kestrel_has_primary']).lower(),
                str(r['em_has_primary_signal']).lower(),
                " | ".join(r['kestrel_folders']),
                " | ".join(r['em_folders'])
            ])

    print(f"kestrel_rows={len(kres)} em_rows={len(emres)} common={common_count}")
    print(f"window_hours={args.window_hours} anchor={anchor_iso or 'n/a'} em_latest={window_stats.get('em_latest', 'n/a')}")
    print(
        f"window_filter k_pre={window_stats.get('k_pre', 0)} k_post={window_stats.get('k_post', 0)} "
        f"k_invalid_ts={window_stats.get('k_invalid_ts', 0)} "
        f"e_pre={window_stats.get('e_pre', 0)} e_post={window_stats.get('e_post', 0)} "
        f"e_invalid_ts={window_stats.get('e_invalid_ts', 0)}"
    )
    print(f"kestrel_with_message_id={k_with_mid} em_with_message_id={e_with_mid}")
    if args.match_mode == "fuzzy-subject-time":
        print(
            f"matched_by_message_id={pair_stats['by_mid']} "
            f"matched_by_sender_subject_time={pair_stats['by_sender_subject_time']} "
            f"matched_by_subject_time={pair_stats['by_subject_time']}"
        )
        print(f"primary_signal_mismatch={primary_signal_mismatch}")
    print(f"only_kestrel={only_k_count} only_em={only_e_count} folder_mismatch={len(diffs)}")
    print(f"wrote: {out_dir / 'kestrel_map.csv'}")
    print(f"wrote: {out_dir / 'em_map.csv'}")
    print(f"wrote: {out_dir / 'mapping_diff.csv'}")
    print(f"wrote: {out_dir / 'primary_signal_mismatch.csv'}")


if __name__ == "__main__":
    main()
