#!/usr/bin/env bash
# tests/integration/run_all.sh — stage1 integration test driver
#
# Seeded under hxa-20260424-005 item #10 (integration test suite seed).
# Walks tests/integration/<nn>_*/test.sh, runs each with timeout 60s, records
# PASS/FAIL/SKIP, exits non-zero if any test fails.
#
# Expected failures (until agent A hxa-20260424-005 #1/#3 lands):
#   06_exec_argv_no_shell_expansion
#   07_json_parse_roundtrip

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
HEXA_ROOT="$(cd "$HERE/../.." && pwd)"
export HEXA_BIN="${HEXA_BIN:-$HEXA_ROOT/hexa}"
export HEXA_ROOT
export FIXTURES="$HERE/fixtures"

if [ ! -x "$HEXA_BIN" ]; then
    echo "run_all: HEXA_BIN not executable at $HEXA_BIN" >&2
    exit 2
fi

pass=0
fail=0
skip=0
failed_tests=()

# Iterate in numeric order (01_.., 02_..).
for d in "$HERE"/[0-9][0-9]_*/; do
    [ -d "$d" ] || continue
    name="$(basename "$d")"
    script="$d/test.sh"
    if [ ! -x "$script" ]; then
        echo "[SKIP] $name — test.sh missing or not executable"
        skip=$((skip + 1))
        continue
    fi
    printf '[ RUN] %-55s ' "$name"
    out="$(timeout 60 bash "$script" 2>&1)"
    rc=$?
    if [ $rc -eq 0 ]; then
        echo "PASS"
        pass=$((pass + 1))
    elif [ $rc -eq 77 ]; then
        # 77 reserved for self-skip (agent A dependencies etc.)
        echo "SKIP"
        skip=$((skip + 1))
    elif [ $rc -eq 124 ]; then
        echo "FAIL (timeout 60s)"
        fail=$((fail + 1))
        failed_tests+=("$name (timeout)")
        echo "---- $name output (last 20 lines) ----"
        echo "$out" | tail -20
        echo "---------------------------------------"
    else
        echo "FAIL (rc=$rc)"
        fail=$((fail + 1))
        failed_tests+=("$name (rc=$rc)")
        echo "---- $name output (last 20 lines) ----"
        echo "$out" | tail -20
        echo "---------------------------------------"
    fi
done

total=$((pass + fail + skip))
echo ""
echo "==================================================="
echo "integration summary: total=$total passed=$pass failed=$fail skipped=$skip"
if [ $fail -gt 0 ]; then
    echo "failed tests:"
    for t in "${failed_tests[@]}"; do
        echo "  - $t"
    done
    echo "==================================================="
    exit 1
fi
echo "==================================================="
exit 0
