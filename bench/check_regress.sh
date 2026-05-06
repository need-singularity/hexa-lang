#!/usr/bin/env bash
# bench/check_regress.sh — N01 (docs/hexa_speedup_brainstorm_2026_05_06.ai.md)
#
# Local bench-regression gate. Compares each fixture's *latest valid* run in
# bench/bench_results.jsonl against its trailing-7-run median for that fixture
# (the "baseline" — Q6/N01 doesn't yet timestamp rows, so we use position).
#
# Heuristic (v0): flag if latest median_ns > 1.10x (configurable) baseline median.
# Statistical t-test (N02) is mid-term; this is the ratio-threshold prototype.
#
# Schema produced by tool/ai_native_bench.hexa:
#   {file, iters, warmup, avg_ns, median_ns, min_ns, max_ns}
# Some rows have `void` literals (not legal JSON) — those are skipped as
# "harness-broken" runs. See bench/README.md for void-entry diagnosis.
#
# Usage:
#   bench/check_regress.sh                       # default jsonl, threshold 1.10
#   bench/check_regress.sh --threshold 1.20      # 20% slack
#   bench/check_regress.sh --window 7            # baseline window size
#   bench/check_regress.sh --jsonl path/to.jsonl
#   bench/check_regress.sh --print               # always print per-fixture lines
#
# Exit codes:
#   0   all fixtures within threshold (or no baseline yet)
#   1   one or more fixtures regressed
#   2   gate itself errored (missing file / no python3)

set -u

THRESHOLD="1.10"
WINDOW="7"
JSONL=""
PRINT_ALL="0"

while [ $# -gt 0 ]; do
    case "$1" in
        --threshold) THRESHOLD="$2"; shift 2;;
        --window)    WINDOW="$2";    shift 2;;
        --jsonl)     JSONL="$2";     shift 2;;
        --print)     PRINT_ALL="1";  shift;;
        -h|--help)
            sed -n '2,30p' "$0"
            exit 0;;
        *)
            echo "unknown arg: $1" >&2; exit 2;;
    esac
done

# Locate jsonl: prefer --jsonl, else $HEXA_LANG/bench/bench_results.jsonl,
# else relative to this script.
if [ -z "$JSONL" ]; then
    if [ -n "${HEXA_LANG:-}" ] && [ -f "$HEXA_LANG/bench/bench_results.jsonl" ]; then
        JSONL="$HEXA_LANG/bench/bench_results.jsonl"
    else
        SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
        JSONL="$SCRIPT_DIR/bench_results.jsonl"
    fi
fi

if [ ! -f "$JSONL" ]; then
    echo "check_regress: jsonl not found: $JSONL" >&2
    exit 2
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "check_regress: python3 required" >&2
    exit 2
fi

python3 - "$JSONL" "$THRESHOLD" "$WINDOW" "$PRINT_ALL" <<'PYEOF'
import json
import sys
from collections import defaultdict

jsonl_path = sys.argv[1]
threshold  = float(sys.argv[2])
window     = int(sys.argv[3])
print_all  = sys.argv[4] == "1"

valid_by_file = defaultdict(list)   # file -> list[median_ns] in file order
void_count = 0
parse_fail = 0
total_rows = 0

with open(jsonl_path, "r") as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        total_rows += 1
        # tool/ai_native_bench.hexa emits literal `void` for failed timings,
        # which is invalid JSON. Detect and count those before json.loads.
        if "\"avg_ns\": void" in line or "\"median_ns\": void" in line:
            void_count += 1
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            parse_fail += 1
            continue
        f_name = row.get("file", "?")
        med = row.get("median_ns")
        if not isinstance(med, (int, float)) or med <= 0:
            void_count += 1
            continue
        # Skip rows where "file" is actually a flag (argv parser bug in
        # ai_native_bench.hexa: --warmup=0 fell through to the file list).
        if isinstance(f_name, str) and f_name.startswith("--"):
            continue
        valid_by_file[f_name].append(int(med))

def median(xs):
    s = sorted(xs)
    n = len(s)
    if n == 0:
        return None
    if n % 2 == 1:
        return s[n // 2]
    return (s[n // 2 - 1] + s[n // 2]) / 2.0

print(f"check_regress: rows_total={total_rows} void={void_count} "
      f"parse_fail={parse_fail} fixtures_with_valid={len(valid_by_file)} "
      f"threshold={threshold} window={window}")
if void_count > 0:
    print(f"check_regress: WARN — {void_count} void/invalid row(s); see bench/README.md")

regressed = []
checked   = 0
no_base   = 0

for f_name, samples in sorted(valid_by_file.items()):
    if len(samples) < 2:
        no_base += 1
        if print_all:
            print(f"  [skip] {f_name}: only {len(samples)} valid sample(s) — no baseline")
        continue
    latest   = samples[-1]
    baseline_pool = samples[-(window + 1):-1]  # up to `window` prior runs
    if not baseline_pool:
        no_base += 1
        continue
    base_med = median(baseline_pool)
    ratio = latest / base_med if base_med else float("inf")
    checked += 1
    status = "OK"
    if ratio > threshold:
        status = "REGRESS"
        regressed.append((f_name, latest, base_med, ratio))
    if print_all or status == "REGRESS":
        print(f"  [{status}] {f_name}: latest={latest}ns base_med={base_med:.0f}ns "
              f"ratio={ratio:.3f}x (n_base={len(baseline_pool)})")

print(f"check_regress: checked={checked} no_baseline={no_base} regressed={len(regressed)}")
if regressed:
    print("check_regress: FAIL")
    sys.exit(1)
print("check_regress: PASS")
sys.exit(0)
PYEOF
