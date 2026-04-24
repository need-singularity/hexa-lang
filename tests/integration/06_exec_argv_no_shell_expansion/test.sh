#!/usr/bin/env bash
# 06_exec_argv_no_shell_expansion — expects the `exec_argv` builtin to pass argv
# verbatim (no shell interpolation), so '$HOME' stays literal.
#
# DEPENDS: agent A hxa-20260424-005 #1/#3 — not yet landed.
# Expected to FAIL until exec_argv is wired into the stage0 interpreter.

set -u
PROBE="$(mktemp -t hxa_exec_argv_XXXXXX).hexa"
trap 'rm -f "$PROBE"' EXIT

cat > "$PROBE" <<'HEXA'
fn main() {
    let r = exec_argv(["echo", "$HOME"])
    println("OUT=" + to_string(r))
}
main()
HEXA

out="$("$HEXA_BIN" "$PROBE" 2>&1)"
rc=$?

# Must: (a) succeed rc=0, (b) output contains literal "$HOME" (no expansion).
if [ $rc -ne 0 ]; then
    echo "FAIL: probe rc=$rc — exec_argv likely not implemented"
    echo "$out"
    exit 1
fi
if echo "$out" | grep -q '\$HOME'; then
    echo "PASS: exec_argv preserved literal \$HOME (no shell expansion)"
    exit 0
fi
echo "FAIL: \$HOME was expanded or not preserved; output:"
echo "$out"
exit 1
