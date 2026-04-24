#!/usr/bin/env bash
# 02_cross_repo_floor_check_selftest — `tool/cross_repo_floor_check.hexa --selftest`
# Guards hxa-20260424-006 (args.len() void silent-fail) end-to-end for a hexa-lang tool.

set -u
SCRIPT="$HEXA_ROOT/tool/cross_repo_floor_check.hexa"
if [ ! -f "$SCRIPT" ]; then
    echo "FAIL: $SCRIPT missing"
    exit 1
fi
out="$("$HEXA_BIN" "$SCRIPT" --selftest 2>&1)"
rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: rc=$rc"
    echo "$out"
    exit 1
fi
case "$out" in
    *"cross_repo_floor_check: selftest PASS"*)
        echo "PASS: selftest emitted expected marker"
        exit 0
        ;;
esac
echo "FAIL: missing 'selftest PASS' marker; output:"
echo "$out"
exit 1
