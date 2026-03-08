#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_PATH="${LOG_PATH:-$HOME/.local/share/kestrel-mail/logs/sync-events.jsonl}"
TAIL="${TAIL:-400}"
WARN_MISS_RATE="${WARN_MISS_RATE:-25}"
WARN_DELTA="${WARN_DELTA:-15}"

"$ROOT_DIR/tools/analyze_sync_events.py" \
  --log "$LOG_PATH" \
  --tail "$TAIL" \
  --warn-miss-rate "$WARN_MISS_RATE" \
  --warn-delta "$WARN_DELTA"
