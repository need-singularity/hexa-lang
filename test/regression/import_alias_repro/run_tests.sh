#!/usr/bin/env bash
# import_alias_repro/run_tests.sh — regression tests for
#   feat/import-alias-runtime-fix-and-array-index-mut
#
# Tests:
#   1. `import "x.hexa" as alias` + `alias::fn()` resolves to fn (was silent void)
#   2. multi-fn alias dispatch
#   3. unregistered alias surfaces an explicit "not found" message (Issue 3)
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

t1="$("$INTERP" "$HERE/b.hexa" 2>&1 | head -1)"
expect "alias_dispatch" "42" "$t1"

t2="$("$INTERP" "$HERE/use_multi.hexa" 2>&1)"
expect "alias_multi_fn" "$(printf '5\n20\nhi from multi')" "$t2"

t3="$("$INTERP" "$HERE/c_missing.hexa" 2>&1 | head -1)"
expect "alias_explicit_error" "[hexa] symbol 'zzz::nope' not found (import alias may be unregistered)" "$t3"

if [ "$fail" -gt 0 ]; then
    echo "$fail test(s) failed"
    exit 1
fi
echo "All import-alias regression tests passed."
