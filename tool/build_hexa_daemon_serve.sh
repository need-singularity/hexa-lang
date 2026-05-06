#!/bin/bash
# build_hexa_daemon_serve.sh — compile tool/hexa_daemon_serve.c → bin/hexa-daemon-serve
#
# Plan: docs/hexa_daemon_option_a_plan_2026_05_06.ai.md
#
# Standalone C binary; intentionally does NOT link against runtime.c (the
# hexa runtime). hexa.real is invoked as a subprocess via execv. This
# isolates daemon listener changes from the hexa transpile pipeline.

set -eu

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO_ROOT/tool/hexa_daemon_serve.c"
OUT_DIR="$REPO_ROOT/bin"
OUT="$OUT_DIR/hexa-daemon-serve"

mkdir -p "$OUT_DIR"

CLANG="${CLANG:-clang}"
UNAME="$(uname 2>/dev/null | tr -d ' \n\t')"

if [ "$UNAME" = "Linux" ]; then
    CFLAGS="-O2 -std=gnu11 -D_GNU_SOURCE -Wall -Wextra"
    LDFLAGS=""
else
    CFLAGS="-O2 -std=c11 -Wall -Wextra"
    LDFLAGS=""
fi

echo "[build_hexa_daemon_serve] uname=$UNAME"
echo "[build_hexa_daemon_serve] $CLANG $CFLAGS $SRC -o $OUT $LDFLAGS"

"$CLANG" $CFLAGS "$SRC" -o "$OUT" $LDFLAGS

# macOS: ad-hoc codesign — same pattern as build_dispatch.hexa.
if [ "$UNAME" = "Darwin" ]; then
    if command -v codesign >/dev/null 2>&1; then
        codesign --force --sign - "$OUT" 2>/dev/null || true
    fi
fi

OUT_SIZE="$(wc -c < "$OUT" | tr -d ' ')"
echo "[build_hexa_daemon_serve] OK -> $OUT ($OUT_SIZE bytes)"
