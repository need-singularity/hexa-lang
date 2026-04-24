#!/usr/bin/env bash
# 07_json_parse_roundtrip — json_parse → json_stringify round-trip for a canonical
# minimal object. Asserts field recovery via hexa_json_parse + map index ops.
#
# DEPENDS: agent A hxa-20260424-005 #1/#3 — structured map-accessor + stringify
# are not yet exposed; this test is expected to FAIL until they land.

set -u
PROBE="$(mktemp -t hxa_json_rt_XXXXXX).hexa"
trap 'rm -f "$PROBE"' EXIT

cat > "$PROBE" <<'HEXA'
fn main() {
    let src = "{\"a\":1,\"b\":\"hi\"}"
    let obj = json_parse(src)
    let back = json_stringify(obj)
    println("BACK=" + back)
    // index round-trip
    let a = obj["a"]
    println("A=" + to_string(a))
    let b = obj["b"]
    println("B=" + to_string(b))
}
main()
HEXA

out="$("$HEXA_BIN" "$PROBE" 2>&1)"
rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: probe rc=$rc — json round-trip path likely missing"
    echo "$out"
    exit 1
fi

ok=1
for needle in 'BACK=' 'A=1' 'B=hi'; do
    if ! echo "$out" | grep -q "$needle"; then
        echo "FAIL: expected '$needle' in output"
        ok=0
    fi
done
if [ $ok -eq 1 ]; then
    echo "PASS: json_parse/json_stringify round-trip works"
    exit 0
fi
echo "---- output ----"
echo "$out"
exit 1
