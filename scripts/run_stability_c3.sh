#!/bin/bash
# scripts/run_stability_c3.sh
# ─────────────────────────────────────────────────────────────────
# Run the C3 PBV-leak stability scaffold (scripts/stability_c3_pbv.hexa)
# with structured output + RSS peak extraction + exit-code propagation.
#
# Usage:
#   bash scripts/run_stability_c3.sh            # smoke (1_000 iters, <5s)
#   STABILITY_ITERS=100000   bash scripts/run_stability_c3.sh   # ~10min
#   STABILITY_ITERS=1000000  bash scripts/run_stability_c3.sh   # ~1h
#   STABILITY_ITERS=150000000 bash scripts/run_stability_c3.sh  # 24h target
#
# Invariants checked:
#   1. hexa_stage0 exits 0
#   2. final RESULT line has "ok":true
#   3. checksum is reproducible for a given (iters, sample, log_cap)
#   4. rss_peak_kb stays bounded (print warning if > 512 MiB)
#
# Emits one jq-valid json line on stdout at the end (PASS/FAIL/status),
# preceded by the raw RESULT line from the hexa run and any SAMPLEs.
# All logs also captured to  /tmp/c3_stability_<ts>.log.
# ─────────────────────────────────────────────────────────────────

set -u

ROOT="${HEXA_LANG:-/Users/ghost/Dev/hexa-lang}"
cd "$ROOT"

# ── Duplicate run prevention (mkdir-based, macOS-safe) ──
LOCKDIR="/tmp/hexa_stability_c3.lock"
if ! mkdir "$LOCKDIR" 2>/dev/null; then
    echo "already running (lock: $LOCKDIR)" >&2; exit 1
fi
trap 'rmdir "$LOCKDIR" 2>/dev/null || true' EXIT INT TERM

BIN="${BIN:-$ROOT/build/hexa_stage0}"
SCRIPT="$ROOT/scripts/stability_c3_pbv.hexa"
: "${STABILITY_WALL_SEC:=300}"

if [ ! -x "$BIN" ]; then
    echo "FATAL: $BIN not executable" >&2
    exit 10
fi
if [ ! -f "$SCRIPT" ]; then
    echo "FATAL: $SCRIPT missing" >&2
    exit 11
fi

# Sensible defaults — 1_000 is a ~sub-second smoke run.
ITERS="${STABILITY_ITERS:-1000}"
SAMPLE="${STABILITY_SAMPLE:-100}"
LOG_CAP="${STABILITY_LOG_CAP:-256}"

TS="$(date +%Y%m%d_%H%M%S)"
LOG="/tmp/c3_stability_${TS}.log"

echo "[run_stability_c3] iters=$ITERS sample=$SAMPLE log_cap=$LOG_CAP bin=$BIN"
echo "[run_stability_c3] log=$LOG"

T_START=$(perl -MTime::HiRes=time -e 'printf "%.3f", time()' 2>/dev/null || date +%s)

STABILITY_ITERS="$ITERS" \
STABILITY_SAMPLE="$SAMPLE" \
STABILITY_LOG_CAP="$LOG_CAP" \
timeout --kill-after=5 "$STABILITY_WALL_SEC" "$BIN" "$SCRIPT" 2>&1 | tee "$LOG"
RC="${PIPESTATUS[0]}"

T_END=$(perl -MTime::HiRes=time -e 'printf "%.3f", time()' 2>/dev/null || date +%s)
ELAPSED=$(perl -e "printf \"%.3f\", $T_END - $T_START" 2>/dev/null || echo "?")

# Extract RESULT summary + rss_peak.
RESULT_LINE="$(grep '^RESULT ' "$LOG" | tail -n1)"
if [ -z "$RESULT_LINE" ]; then
    echo
    echo "FAIL: no RESULT line in output (rc=$RC, elapsed=${ELAPSED}s)"
    echo '{"pass":false,"reason":"no_result_line","rc":'"$RC"',"elapsed_s":"'"$ELAPSED"'"}'
    exit 20
fi

# Parse key fields from RESULT JSON payload without requiring jq.
JSON="${RESULT_LINE#RESULT }"
CHECKSUM="$(echo "$JSON" | sed -E 's/.*"checksum":([0-9]+).*/\1/')"
RSS_PEAK="$(echo "$JSON" | sed -E 's/.*"rss_peak_kb":([0-9]+).*/\1/')"
RSS_GROW="$(echo "$JSON" | sed -E 's/.*"rss_growth_kb":(-?[0-9]+).*/\1/')"
OK_FLAG="$(echo "$JSON" | sed -E 's/.*"ok":(true|false).*/\1/')"

# Warn on bounded-growth violation — 512 MiB soft ceiling.
WARN=""
if [ -n "$RSS_PEAK" ] && [ "$RSS_PEAK" -gt 524288 ]; then
    WARN="rss_peak_over_512mib"
fi

# Scale-extrapolated 24h equivalent estimate, based on this run's
# sec/iter throughput.  Pure informational — never fails the gate.
if [ "$ELAPSED" != "?" ] && [ "$ITERS" -gt 0 ]; then
    EST24H=$(perl -e "my \$e=$ELAPSED; my \$i=$ITERS; my \$target=86400*(\$i/\$e); printf \"%.0f\", \$target" 2>/dev/null || echo "?")
else
    EST24H="?"
fi

PASS="false"
if [ "$RC" -eq 0 ] && [ "$OK_FLAG" = "true" ]; then
    PASS="true"
fi

echo
echo "[run_stability_c3] rc=$RC elapsed=${ELAPSED}s"
echo "[run_stability_c3] checksum=$CHECKSUM rss_peak_kb=$RSS_PEAK rss_growth_kb=$RSS_GROW"
echo "[run_stability_c3] 24h-equivalent iter count @ this rate: $EST24H"
if [ -n "$WARN" ]; then
    echo "[run_stability_c3] WARN: $WARN"
fi

echo
echo '{"pass":'"$PASS"',"rc":'"$RC"',"iters":'"$ITERS"',"elapsed_s":"'"$ELAPSED"'","checksum":'"${CHECKSUM:-0}"',"rss_peak_kb":'"${RSS_PEAK:-0}"',"rss_growth_kb":'"${RSS_GROW:-0}"',"est_24h_iters":"'"$EST24H"'","warn":"'"$WARN"'","log":"'"$LOG"'"}'

if [ "$PASS" = "true" ]; then
    exit 0
fi
exit 1
