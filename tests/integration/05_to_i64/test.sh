#!/usr/bin/env bash
# 05_to_i64 — guards hxa-20260424-006 (to_i64 missing / strict-throw regression).
# Asserts: to_i64("42")==42, to_i64("") and to_i64("garbage") return 0 (permissive
# fallback — matches dormancy_wake_tick try-catch semantics).

set -u
PROBE="$(mktemp -t hxa_to_i64_XXXXXX).hexa"
trap 'rm -f "$PROBE"' EXIT

cat > "$PROBE" <<'HEXA'
fn main() {
    let a = to_i64("42")
    println("A=" + to_string(a))
    let b = to_i64("")
    println("B=" + to_string(b))
    let c = to_i64("garbage")
    println("C=" + to_string(c))
    let d = to_i64("-7")
    println("D=" + to_string(d))
}
main()
HEXA

out="$("$HEXA_BIN" "$PROBE" 2>&1)"
rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: probe rc=$rc"
    echo "$out"
    exit 1
fi

ok=1
for needle in "A=42" "B=0" "C=0" "D=-7"; do
    if ! echo "$out" | grep -q "^$needle$"; then
        echo "FAIL: expected '$needle' in output"
        ok=0
    fi
done

if [ $ok -eq 1 ]; then
    echo "PASS: to_i64 handles valid/empty/garbage/negative"
    exit 0
fi
echo "---- output ----"
echo "$out"
exit 1
