#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/tmp/mapping-compare}"
TOP="${TOP:-40}"
EM_BASE="${EM_BASE:-$HOME/Documents/eM Client/e985e67e-a27f-4193-9786-3778a07f0e3f/6a50b32f-3cb7-4408-a6c6-f9e319c30a40}"

"$ROOT_DIR/tools/probe_only_em_presence.py" \
  --diff "$OUT_DIR/mapping_diff.csv" \
  --em-mail-index "$EM_BASE/mail_index.dat" \
  --top "$TOP"
