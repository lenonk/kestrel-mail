#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

KESTREL_DB="${KESTREL_DB:-$HOME/.local/share/kestrel-mail/mail.db}"
EM_BASE="${EM_BASE:-$HOME/Documents/eM Client/e985e67e-a27f-4193-9786-3778a07f0e3f/6a50b32f-3cb7-4408-a6c6-f9e319c30a40}"
EM_MAIL_INDEX="${EM_MAIL_INDEX:-$EM_BASE/mail_index.dat}"
EM_FOLDERS_DB="${EM_FOLDERS_DB:-$EM_BASE/folders.dat}"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/tmp/mapping-compare}"
LIMIT="${LIMIT:-0}"
MATCH_MODE="${MATCH_MODE:-fuzzy-subject-time}"
WINDOW_HOURS="${WINDOW_HOURS:-72}"

"$ROOT_DIR/tools/compare_mappings.py" \
  --kestrel-db "$KESTREL_DB" \
  --em-mail-index "$EM_MAIL_INDEX" \
  --em-folders-db "$EM_FOLDERS_DB" \
  --limit "$LIMIT" \
  --out-dir "$OUT_DIR" \
  --match-mode "$MATCH_MODE" \
  --window-hours "$WINDOW_HOURS"

echo
echo "Done. Artifacts:"
echo "  $OUT_DIR/kestrel_map.csv"
echo "  $OUT_DIR/em_map.csv"
echo "  $OUT_DIR/mapping_diff.csv"
