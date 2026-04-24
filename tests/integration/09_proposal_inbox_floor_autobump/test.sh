#!/usr/bin/env bash
# 09_proposal_inbox_floor_autobump — exercises the cross_repo_blocker floor (>=95)
# via the hermetic selftest. proposal_inbox.hexa --selftest covers the
# floor logic without touching any real inventory file (ingress-only by design).

set -u
SCRIPT="$HEXA_ROOT/tool/proposal_inbox.hexa"
if [ ! -f "$SCRIPT" ]; then
    echo "FAIL: $SCRIPT missing"
    exit 1
fi
out="$("$HEXA_BIN" "$SCRIPT" --selftest 2>&1)"
rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: selftest rc=$rc"
    echo "$out"
    exit 1
fi
case "$out" in
    *"proposal_inbox: selftest PASS"*)
        echo "PASS: proposal_inbox selftest — floor autobump logic exercised"
        exit 0
        ;;
esac
echo "FAIL: missing 'selftest PASS' marker; output:"
echo "$out"
exit 1
