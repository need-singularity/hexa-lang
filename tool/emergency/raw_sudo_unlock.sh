#!/bin/bash
# tool/emergency/raw_sudo_unlock.sh — sudo chflags nouchg all SSOT
#
# Use when: hx_unlock refuses (raw_all bug), developer must edit
# SSOT immediately. Leaves tree unlocked — developer must manually
# ./hexa tool/hx_lock.hexa --force --reason "post-emergency" after.
#
# Requires: sudo (to override chflags uchg on Darwin)
#
# raw#8 exception: same as raw_reset.sh — bash is the only runtime
# guaranteed to work when raw_all / hx_unlock is broken.
#
# Behavior:
#   1. Read REASON from stdin (mandatory, non-empty).
#   2. sudo chflags nouchg on the 5 SSOT files (Darwin only).
#   3. Append audit line to .raw-audit.
#   4. Print reminder: developer MUST re-lock via hx_lock afterwards.
#
# Exit codes:
#   0 = success
#   2 = empty reason (aborted)
#   3 = not inside repo root (no .raw present)
#   4 = chflags failed

set -euo pipefail

SSOT_FILES=(.raw .own .ext .roadmap)
AUDIT_FILE=".raw-audit"

if [ ! -f .raw ]; then
    echo "raw_sudo_unlock: must be run from hexa-lang repo root (.raw not found)" >&2
    exit 3
fi

# Mandatory reason prompt
read -r -p "Reason (logged to .raw-audit): " REASON
if [ -z "${REASON// /}" ]; then
    echo "raw_sudo_unlock: empty reason — aborted" >&2
    exit 2
fi

PLATFORM="$(uname -s)"
if [ "$PLATFORM" = "Darwin" ]; then
    echo "raw_sudo_unlock: unlocking SSOT via sudo chflags nouchg..."
    sudo chflags nouchg "${SSOT_FILES[@]}" || {
        echo "raw_sudo_unlock: chflags nouchg failed" >&2
        exit 4
    }
else
    echo "raw_sudo_unlock: WARNING non-Darwin platform ($PLATFORM) — chflags skipped" >&2
fi

# Audit append
TS="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
WHO="${USER:-unknown}"
printf 'EMERGENCY unlock | %s | %s | forced | %s\n' "$WHO" "$REASON" "$TS" >> "$AUDIT_FILE"

echo "Tree is now UNLOCKED. Run ./hexa tool/hx_lock.hexa when done."
exit 0
