#!/usr/bin/env bash
# array_idx_assign_repro/run_tests.sh — regression tests for indexed
# array mutation. These currently PASS on the live interpreter (the
# fix landed in an earlier commit, see commit ea736c1d "interp resolver
# fixes"); this script captures the contract so future regressions are
# caught immediately.
#
# Usage: HEXA_INTERP=/path/to/hexa_interp.real ./run_tests.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
INTERP="${HEXA_INTERP:-$HERE/../../../build/hexa_interp.real}"
if [ ! -x "$INTERP" ]; then
    echo "FAIL: HEXA_INTERP not executable: $INTERP" >&2
    exit 1
fi
HEXA_LANG="$(cd "$HERE/../../.." && pwd)"
export HEXA_LANG

fail=0
expect() {
    local label="$1" expected="$2" actual="$3"
    if [ "$expected" = "$actual" ]; then
        echo "PASS: $label"
    else
        echo "FAIL: $label"
        echo "  expected: $expected"
        echo "  actual:   $actual"
        fail=$((fail + 1))
    fi
}

t1="$("$INTERP" "$HERE/c.hexa" 2>&1 | head -1)"
expect "indexed_assign" "99" "$t1"

t2="$("$INTERP" "$HERE/c_oob.hexa" 2>&1 | head -1)"
expect "indexed_assign_oob_bounds_check" "index 10 out of bounds (len 3)" "$t2"

if [ "$fail" -gt 0 ]; then
    echo "$fail test(s) failed"
    exit 1
fi
echo "All array-index-assign regression tests passed."
