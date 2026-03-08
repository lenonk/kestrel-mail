#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/tmp/mapping-compare}"
MAPPING_DIFF="$OUT_DIR/mapping_diff.csv"
SYNC_SUMMARY="$OUT_DIR/sync_summary.txt"
MAPPING_SUMMARY="$OUT_DIR/mapping_summary.txt"
ONLY_EM_PROBE="$OUT_DIR/only_em_probe.txt"

MAX_FOLDER_MISMATCH="${MAX_FOLDER_MISMATCH:-0}"
MAX_ONLY_KESTREL="${MAX_ONLY_KESTREL:-0}"
MAX_ONLY_EM="${MAX_ONLY_EM:-0}"
MAX_SYNC_WARNINGS="${MAX_SYNC_WARNINGS:-0}"
MAX_SYNC_FAILS="${MAX_SYNC_FAILS:-0}"

echo "== Kestrel Parity Report =="
echo ""

echo "[1/3] Running mapping comparator..."
"$ROOT_DIR/tools/compare_latest.sh"

echo ""
echo "[2/4] Running sync event analyzer..."
"$ROOT_DIR/tools/analyze_sync_latest.sh" | tee "$SYNC_SUMMARY"

echo ""
echo "[3/5] Running mapping gap analyzer..."
"$ROOT_DIR/tools/analyze_mapping_latest.sh" | tee "$MAPPING_SUMMARY" >/dev/null

echo ""
echo "[4/5] Probing only_em presence in Kestrel by Message-ID..."
"$ROOT_DIR/tools/probe_only_em_latest.sh" | tee "$ONLY_EM_PROBE" >/dev/null

echo ""
echo "[5/5] Computing summary..."

if [[ ! -f "$MAPPING_DIFF" ]]; then
  echo "ERROR: mapping diff file missing: $MAPPING_DIFF"
  exit 2
fi

only_kestrel=$(awk -F, 'NR>1 && $1=="only_kestrel" {c++} END{print c+0}' "$MAPPING_DIFF")
only_em=$(awk -F, 'NR>1 && $1=="only_em" {c++} END{print c+0}' "$MAPPING_DIFF")
folder_mismatch=$(awk -F, 'NR>1 && $1=="folder_mismatch" {c++} END{print c+0}' "$MAPPING_DIFF")

sync_warnings=$(awk -F= '/^instability_warnings=/{print $2}' "$SYNC_SUMMARY" | tail -n1)
sync_warnings=${sync_warnings:-0}
sync_fails=$(awk -F= '/^sync_finish_fail=/{print $2}' "$SYNC_SUMMARY" | tail -n1)
sync_fails=${sync_fails:-0}

echo "mapping.only_kestrel=$only_kestrel"
echo "mapping.only_em=$only_em"
echo "mapping.folder_mismatch=$folder_mismatch"
echo "sync.failures=$sync_fails"
echo "sync.instability_warnings=$sync_warnings"
echo "artifacts.sync_summary=$SYNC_SUMMARY"
echo "artifacts.mapping_summary=$MAPPING_SUMMARY"
echo "artifacts.only_em_probe=$ONLY_EM_PROBE"

pass=true
if (( folder_mismatch > MAX_FOLDER_MISMATCH )); then pass=false; fi
if (( only_kestrel > MAX_ONLY_KESTREL )); then pass=false; fi
if (( only_em > MAX_ONLY_EM )); then pass=false; fi
if (( sync_warnings > MAX_SYNC_WARNINGS )); then pass=false; fi
if (( sync_fails > MAX_SYNC_FAILS )); then pass=false; fi

echo ""
if [[ "$pass" == "true" ]]; then
  echo "PARITY_STATUS=PASS"
  echo "Thresholds: folder_mismatch<=$MAX_FOLDER_MISMATCH only_kestrel<=$MAX_ONLY_KESTREL only_em<=$MAX_ONLY_EM sync_warnings<=$MAX_SYNC_WARNINGS sync_fails<=$MAX_SYNC_FAILS"
  exit 0
else
  echo "PARITY_STATUS=FAIL"
  echo "Thresholds: folder_mismatch<=$MAX_FOLDER_MISMATCH only_kestrel<=$MAX_ONLY_KESTREL only_em<=$MAX_ONLY_EM sync_warnings<=$MAX_SYNC_WARNINGS sync_fails<=$MAX_SYNC_FAILS"
  exit 1
fi
