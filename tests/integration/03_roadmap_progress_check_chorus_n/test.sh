#!/usr/bin/env bash
# 03_roadmap_progress_check_chorus_n — `tool/roadmap_progress_check.hexa --selftest`.
# R33 chorus_n emission surface is exercised via the hermetic selftest path
# (avoids touching the live .roadmap file). Selftest must exit 0 and print
# the canonical OK marker.

set -u
SCRIPT="$HEXA_ROOT/tool/roadmap_progress_check.hexa"
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
    *"[roadmap_progress_check selftest] OK"*)
        echo "PASS: selftest OK marker present"
        exit 0
        ;;
esac
echo "FAIL: missing selftest OK marker; output:"
echo "$out"
exit 1
