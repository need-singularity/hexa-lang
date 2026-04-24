#!/usr/bin/env bash
# 04_args_len — guards hxa-20260424-006 (args.len() → void silent-fail regression).
# Builds a one-shot probe script and asserts args.len() returns a live int
# and argv indices are correctly exposed.

set -u
PROBE="$(mktemp -t hxa_args_len_XXXXXX).hexa"
trap 'rm -f "$PROBE"' EXIT

cat > "$PROBE" <<'HEXA'
fn main() {
    let n = args.len()
    println("ARGS_LEN=" + to_string(n))
    if n >= 2 {
        println("ARG1=" + args[1])
    }
    if n >= 3 {
        println("ARG2=" + args[2])
    }
}
main()
HEXA

out="$("$HEXA_BIN" "$PROBE" alpha beta 2>&1)"
rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: probe rc=$rc"
    echo "$out"
    exit 1
fi

# Interpreter re-runs main() once (stage0 quirk), so each line may appear twice.
# We just need at least one match for each expected line.
ok=1
for needle in "ARGS_LEN=3" "ARG1=alpha" "ARG2=beta"; do
    if ! echo "$out" | grep -q "^$needle$"; then
        echo "FAIL: expected line '$needle' not in output"
        ok=0
    fi
done

if [ $ok -eq 1 ]; then
    echo "PASS: args.len() returns int=3, argv indices resolve"
    exit 0
fi
echo "---- output ----"
echo "$out"
exit 1
