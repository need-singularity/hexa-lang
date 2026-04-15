#!/usr/bin/env bash
# hexa_link.sh — M1 link step for Separate Compilation
#
# Thin clang wrapper. Takes N .c files (transpiled by `hexa build --c-only`
# or equivalent) and produces a native executable. Paired .hxi sidecars
# are optional at link time — clang handles C-level symbol resolution
# natively. .hxi validation (signature consistency across units) is a
# post-M1 concern.
#
# Usage:
#     scripts/hexa_link.sh -o <out> <a.c> [b.c ...] [-- clang-extra-flags...]
#
# Defaults:
#     CFLAGS="-O2 -I $HEXA_LANG/self"
#     OS-specific flags auto-appended (macOS codesign).
#
# First milestone: minimum viable linker. Future work:
#     M2 — .hxi cross-unit signature validation (this script can probe)
#     M3 — runtime.c dedup (currently each .c #include "runtime.c", duplicate
#          emits across units would conflict — rely on weak/common attr for now)

set -e

OUT=""
C_FILES=()
EXTRA_FLAGS=()
PAST_DOUBLE_DASH=0

while [ $# -gt 0 ]; do
    if [ "$PAST_DOUBLE_DASH" = "1" ]; then
        EXTRA_FLAGS+=("$1"); shift; continue
    fi
    case "$1" in
        -o)      OUT="$2"; shift 2 ;;
        --)      PAST_DOUBLE_DASH=1; shift ;;
        -h|--help)
            echo "usage: scripts/hexa_link.sh -o <out> <a.c> [b.c ...] [-- clang-flags...]"
            exit 0
            ;;
        *)       C_FILES+=("$1"); shift ;;
    esac
done

if [ -z "$OUT" ] || [ ${#C_FILES[@]} -eq 0 ]; then
    echo "usage: scripts/hexa_link.sh -o <out> <a.c> [b.c ...] [-- clang-flags...]" >&2
    exit 1
fi

HEXA_DIR="${HEXA_LANG:-$(cd "$(dirname "$0")/.." && pwd)}"
CLANG="${CLANG:-clang}"
UNAME="$(uname 2>/dev/null | tr -d ' \n\t')"

case "$UNAME" in
    Linux)
        CFLAGS=(-O2 -std=gnu11 -D_GNU_SOURCE -I "$HEXA_DIR/self")
        LDFLAGS=(-lm -ldl)
        ;;
    Darwin|*)
        CFLAGS=(-O2 -std=c11 -I "$HEXA_DIR/self")
        LDFLAGS=()
        ;;
esac

echo "[hexa_link] uname=$UNAME files=${#C_FILES[@]} out=$OUT"
# shellcheck disable=SC2086
"$CLANG" "${CFLAGS[@]}" -Wno-trigraphs "${C_FILES[@]}" -o "$OUT" "${LDFLAGS[@]}" "${EXTRA_FLAGS[@]}"

if [ "$UNAME" = "Darwin" ] && command -v codesign >/dev/null 2>&1; then
    codesign --force --sign - "$OUT" 2>/dev/null || true
fi

echo "[hexa_link] OK -> $OUT"
