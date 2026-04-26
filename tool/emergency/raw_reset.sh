#!/bin/bash
# tool/emergency/raw_reset.sh — restore 5 SSOT from git HEAD
#
# Use when: .raw is corrupted, raw_loader rejects valid content,
# or any SSOT file is in a broken state blocking unlock ceremony.
#
# Requires: sudo (to override chflags uchg on Darwin)
#
# Scope: this script operates ONLY on the 5 tracked SSOT files
#   .raw  .own  .ext  .roadmap
# It does NOT touch .raw-audit (append-only — preserved across resets).
#
# raw#8 exception: this is a bash script in a HEXA-ONLY repo. The
# emergency kit must work when hexa/stage0/raw_loader is broken; bash
# is pre-installed on every supported dev host. No .hexa equivalent
# can run in the failure mode this script targets.
#
# Behavior:
#   1. Detect platform (chflags only needed on Darwin).
#   2. Unlock SSOT (sudo chflags nouchg) if Darwin.
#   3. git checkout HEAD -- <5 SSOT files>  (hard-restore from index).
#   4. Re-lock SSOT (sudo chflags uchg) if Darwin.
#   5. Append an audit line to .raw-audit.
#
# Exit codes:
#   0 = success
#   1 = git checkout failed
#   2 = chflags failed
#   3 = not inside repo root (no .raw present)

set -euo pipefail

SSOT_FILES=(.raw .own .ext .roadmap)
AUDIT_FILE=".raw-audit"

if [ ! -f .raw ]; then
    echo "raw_reset: must be run from hexa-lang repo root (.raw not found)" >&2
    exit 3
fi

PLATFORM="$(uname -s)"
DARWIN=0
if [ "$PLATFORM" = "Darwin" ]; then
    DARWIN=1
else
    echo "raw_reset: WARNING non-Darwin platform ($PLATFORM) — chflags skipped" >&2
fi

# Step 1 — unlock (Darwin only)
if [ "$DARWIN" = "1" ]; then
    echo "raw_reset: unlocking SSOT via sudo chflags nouchg..."
    sudo chflags nouchg "${SSOT_FILES[@]}" || {
        echo "raw_reset: chflags nouchg failed" >&2
        exit 2
    }
fi

# Step 2 — git checkout HEAD for each SSOT
echo "raw_reset: restoring SSOT from git HEAD..."
git checkout HEAD -- "${SSOT_FILES[@]}" || {
    echo "raw_reset: git checkout HEAD failed" >&2
    exit 1
}

# Step 3 — re-lock (Darwin only)
if [ "$DARWIN" = "1" ]; then
    echo "raw_reset: re-locking SSOT via sudo chflags uchg..."
    sudo chflags uchg "${SSOT_FILES[@]}" || {
        echo "raw_reset: chflags uchg failed (tree left unlocked!)" >&2
        exit 2
    }
fi

# Step 4 — audit log
TS="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
WHO="${USER:-unknown}"
printf 'EMERGENCY: raw_reset to HEAD by %s at %s\n' "$WHO" "$TS" >> "$AUDIT_FILE"

echo "raw_reset: OK — 5 SSOT restored to git HEAD, audit appended."
exit 0
