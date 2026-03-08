#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/tmp/mapping-compare}"
TOP="${TOP:-15}"

"$ROOT_DIR/tools/analyze_mapping_diff.py" --diff "$OUT_DIR/mapping_diff.csv" --top "$TOP"
