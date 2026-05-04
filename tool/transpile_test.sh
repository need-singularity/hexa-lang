#!/usr/bin/env bash
# F-TRANSPILE-2 acceptance runner.
#
# For each fixtures/transpile/case_NN_*.hexa:
#   1. transpile to a temp .py via tool/hexa_to_py.py
#   2. byte-compare against case_NN_*.expected.py (round-trip determinism)
#   3. exec the produced .py with python3 (must exit 0)
#
# Emits PASS/FAIL count. Exits 0 iff PASS == TOTAL.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIX="$ROOT/fixtures/transpile"
TRANS="$ROOT/tool/hexa_to_py.py"

if [[ ! -d "$FIX" ]]; then
  echo "no fixture dir: $FIX" >&2
  exit 2
fi

PY="${PYTHON:-python3}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

total=0
pass=0
fail=0
fails=()
for hexa in "$FIX"/case_*.hexa; do
  [[ -f "$hexa" ]] || continue
  total=$((total + 1))
  base="${hexa%.hexa}"
  expected="${base}.expected.py"
  out="$TMP/$(basename "$base").py"

  if ! "$PY" "$TRANS" "$hexa" "$out" >/dev/null 2>&1; then
    fail=$((fail + 1))
    fails+=("transpile: $(basename "$hexa")")
    continue
  fi
  if [[ ! -f "$expected" ]]; then
    fail=$((fail + 1))
    fails+=("missing expected: $(basename "$expected")")
    continue
  fi
  if ! diff -q "$out" "$expected" >/dev/null 2>&1; then
    fail=$((fail + 1))
    fails+=("diff: $(basename "$hexa")")
    continue
  fi
  if ! "$PY" "$out" >/dev/null 2>&1; then
    fail=$((fail + 1))
    fails+=("exec: $(basename "$hexa")")
    continue
  fi
  pass=$((pass + 1))
done

echo "PASS=$pass FAIL=$fail TOTAL=$total"
if (( fail > 0 )); then
  echo "failures:"
  for f in "${fails[@]}"; do
    echo "  - $f"
  done
fi
# F-TRANSPILE-2: assert all 50+ cases pass
if (( total < 50 )); then
  echo "F-TRANSPILE-2 FAIL: only $total cases (need >= 50)" >&2
  exit 1
fi
if (( pass != total )); then
  echo "F-TRANSPILE-2 FAIL: $pass/$total" >&2
  exit 1
fi
echo "F-TRANSPILE-2 OK: $pass/$total"
exit 0
