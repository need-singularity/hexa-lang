#!/usr/bin/env bash
# build_native.sh — hexa -> C -> native binary pipeline
#
# Uses hexa_v2 (self-hosted compiler) to transpile a .hexa source file
# to C, then compiles with clang to a native binary. This is the same
# pipeline that produced the 1,922x vocoder result.
#
# The generated C emits `#include "runtime.c"` which transitively
# includes native/tensor_kernels.c and native/net.c, so we stage
# runtime.c + native/ into the build dir before clang.
#
# Usage:
#   bash scripts/build_native.sh input.hexa              # -> build/input_native
#   bash scripts/build_native.sh input.hexa my_binary     # -> my_binary
#   bash scripts/build_native.sh --clean                  # remove build artifacts
#
# Environment overrides:
#   CC=gcc          bash scripts/build_native.sh foo.hexa   # swap compiler
#   CFLAGS="-O2 -g" bash scripts/build_native.sh foo.hexa  # custom flags
#   HEXA_V2=/path   bash scripts/build_native.sh foo.hexa  # custom compiler

set -e

HEXA_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$HEXA_DIR/build/native_tmp"
HEXA_V2="${HEXA_V2:-$HEXA_DIR/self/native/hexa_v2}"
CC="${CC:-clang}"
UNAME="$(uname -s)"

# ── helpers ────────────────────────────────────────────────────
die()  { echo "[build_native] ERROR: $*" >&2; exit 1; }
info() { echo "[build_native] $*"; }

human_size() {
    local bytes="$1"
    if [ "$bytes" -ge 1048576 ]; then
        echo "$(( bytes / 1048576 )).$((( bytes % 1048576 ) * 10 / 1048576 )) MB"
    elif [ "$bytes" -ge 1024 ]; then
        echo "$(( bytes / 1024 )) KB"
    else
        echo "$bytes bytes"
    fi
}

# ── --clean ────────────────────────────────────────────────────
if [ "${1:-}" = "--clean" ]; then
    info "cleaning $BUILD_DIR"
    rm -rf "$BUILD_DIR"
    info "done"
    exit 0
fi

# ── argument parsing ──────────────────────────────────────────
INPUT="${1:-}"
if [ -z "$INPUT" ]; then
    echo "Usage: bash scripts/build_native.sh INPUT.hexa [OUTPUT]" >&2
    echo "       bash scripts/build_native.sh --clean" >&2
    exit 1
fi

# resolve to absolute path
case "$INPUT" in
    /*) ;;
    *)  INPUT="$(pwd)/$INPUT" ;;
esac

[ -f "$INPUT" ] || die "input file not found: $INPUT"

# derive output name
if [ -n "${2:-}" ]; then
    OUTPUT="$2"
    case "$OUTPUT" in
        /*) ;;
        *)  OUTPUT="$(pwd)/$OUTPUT" ;;
    esac
else
    BASENAME="$(basename "$INPUT" .hexa)"
    OUTPUT="$HEXA_DIR/build/${BASENAME}_native"
fi

# ── preflight checks ─────────────────────────────────────────
[ -x "$HEXA_V2" ] || die "hexa_v2 compiler not found at $HEXA_V2"
command -v "$CC" >/dev/null 2>&1 || die "$CC not found in PATH"

RUNTIME_C="$HEXA_DIR/self/runtime.c"
NATIVE_DIR="$HEXA_DIR/self/native"
[ -f "$RUNTIME_C" ]                    || die "runtime.c not found at $RUNTIME_C"
[ -f "$NATIVE_DIR/tensor_kernels.c" ]  || die "native/tensor_kernels.c not found"
[ -f "$NATIVE_DIR/net.c" ]             || die "native/net.c not found"

# ── prepare build dir ─────────────────────────────────────────
mkdir -p "$BUILD_DIR/native"
mkdir -p "$(dirname "$OUTPUT")"

# Stage runtime files from working tree. The hexa_v2 compiler emits
# calls to arena/shim functions that must match the runtime version.
# Using git HEAD can desync when codegen evolves ahead of committed runtime.
cp -f "$RUNTIME_C"                    "$BUILD_DIR/runtime.c"
cp -f "$NATIVE_DIR/tensor_kernels.c"  "$BUILD_DIR/native/tensor_kernels.c"
if [ -f "$NATIVE_DIR/net.c" ]; then
    cp -f "$NATIVE_DIR/net.c"         "$BUILD_DIR/native/net.c"
elif git -C "$HEXA_DIR" rev-parse HEAD >/dev/null 2>&1; then
    git -C "$HEXA_DIR" show HEAD:self/native/net.c > "$BUILD_DIR/native/net.c"
fi

# ── step 1: hexa_v2 transpile ────────────────────────────────
GEN_C="$BUILD_DIR/gen.c"
info "transpile: $HEXA_V2 $(basename "$INPUT") -> gen.c"
"$HEXA_V2" "$INPUT" "$GEN_C" || die "hexa_v2 transpile failed"
[ -f "$GEN_C" ] || die "hexa_v2 produced no output"

GEN_SIZE=$(wc -c < "$GEN_C" | tr -d ' ')
info "generated C: $(human_size "$GEN_SIZE")"

# ── step 2: clang compile ────────────────────────────────────
DEFAULT_CFLAGS="-O3"
case "$UNAME" in
    Linux)  DEFAULT_LDFLAGS="-lm -ldl" ;;
    Darwin) DEFAULT_LDFLAGS="-lm -ldl" ;;
    *)      DEFAULT_LDFLAGS="-lm" ;;
esac

CFLAGS="${CFLAGS:-$DEFAULT_CFLAGS}"
LDFLAGS="${LDFLAGS:-$DEFAULT_LDFLAGS}"

# Optionally link hxvocoder.c for native FFI shims (WAV write, decode_nv).
# The codegen emits dlsym() stubs for @link("hxvocoder") declarations;
# statically linking hxvocoder.c makes the symbols resolvable without
# a separate .dylib at runtime.
EXTRA_SRC=""
HXVOCODER_C="$NATIVE_DIR/hxvocoder.c"
if [ -f "$HXVOCODER_C" ]; then
    EXTRA_SRC="$HXVOCODER_C"
    info "linking hxvocoder.c (static FFI shims)"
fi

info "compile: $CC $CFLAGS -I$BUILD_DIR -o $(basename "$OUTPUT") gen.c $LDFLAGS"
$CC $CFLAGS -I"$BUILD_DIR" -o "$OUTPUT" "$GEN_C" $EXTRA_SRC $LDFLAGS \
    || die "clang compilation failed"

[ -x "$OUTPUT" ] || die "compilation produced no executable"

# ── report ────────────────────────────────────────────────────
BIN_SIZE=$(wc -c < "$OUTPUT" | tr -d ' ')
info "OK: $OUTPUT ($(human_size "$BIN_SIZE"))"
