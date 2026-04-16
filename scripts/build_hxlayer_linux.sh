#!/usr/bin/env bash
# build_hxlayer_linux.sh — build libhxlayer.so on Linux, OR run a
# syntactic/link cross-verify on Mac before deploying to the H100 pod.
#
# C2 roadmap — "layer-level fused FFI" Step 1. Companion to
# scripts/build_hxblas_linux.sh. Same flags, same layout, same
# --mac-xverify contract, so operators don't have to learn a new recipe.
#
# Prerequisites (Ubuntu/Debian, runpod/pytorch:2.4.0 image):
#   apt install gcc libm-dev      # libm ships with glibc; no extra deps
#
# Prerequisites (Mac cross-verify mode):
#   (none — libm is in libSystem)
#
# Usage:
#   bash scripts/build_hxlayer_linux.sh                   # normal Linux build
#   bash scripts/build_hxlayer_linux.sh --debug           # -O0 -g for debug
#   bash scripts/build_hxlayer_linux.sh --mac-xverify     # syntactic + link
#                                                         #   verify on Mac
#                                                         #   (.dylib emitted)
#   bash scripts/build_hxlayer_linux.sh --mac-native      # emit Mac-native
#                                                         #   libhxlayer.dylib
#                                                         #   for live FFI
#                                                         #   (C2 Step 2 test)
#   CC=clang bash scripts/build_hxlayer_linux.sh          # use clang instead
#   DEPLOY=/usr/local/lib bash scripts/build_hxlayer_linux.sh  # install
#
# Outputs (Linux mode):
#   self/native/build/libhxlayer.so
#
# Outputs (--mac-xverify mode):
#   self/native/build/xverify/libhxlayer_xverify.dylib
#
# Outputs (--mac-native mode):
#   self/native/build/libhxlayer.dylib   (loadable by ./hexa on Mac)
#
# H100 pod deployment (Linux mode, after `git pull` on the pod):
#   bash scripts/build_hxlayer_linux.sh                                  # build
#   export LD_LIBRARY_PATH=$PWD/self/native/build:$LD_LIBRARY_PATH      # use

set -e

HEXA_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$HEXA_DIR/self/native"
OUT_DIR="$HEXA_DIR/self/native/build"
CC="${CC:-gcc}"
MODE="${1:-build}"

mkdir -p "$OUT_DIR"

# ────────────────────────────────────────────────────────────────
# --mac-xverify : run on Mac to validate the Linux source compiles
# and links cleanly BEFORE committing or pushing to the H100 pod.
# Emits a .dylib in self/native/build/xverify/ alongside the hxblas
# xverify artefacts.
# ────────────────────────────────────────────────────────────────
if [ "$MODE" = "--mac-xverify" ]; then
    UNAME=$(uname -s)
    if [ "$UNAME" != "Darwin" ]; then
        echo "--mac-xverify requires macOS, got $UNAME" >&2
        exit 2
    fi
    XV_DIR="$OUT_DIR/xverify"
    mkdir -p "$XV_DIR"
    # Force clang on Mac (gcc on Mac is just clang anyway, but be explicit)
    CC_X="${CC:-clang}"
    echo "[xverify] linking hxlayer_linux.c → libhxlayer_xverify.dylib"
    $CC_X -O3 -fPIC -dynamiclib -D__linux__=1 \
        "$SRC_DIR/hxlayer_linux.c" \
        -lm \
        -o "$XV_DIR/libhxlayer_xverify.dylib"

    # Smoke: the symbol must be exported.
    if nm -gU "$XV_DIR/libhxlayer_xverify.dylib" | grep -q '_hxlayer_rmsnorm_silu'; then
        echo "  hxlayer_rmsnorm_silu SYMBOL=PRESENT"
    else
        echo "  hxlayer_rmsnorm_silu SYMBOL=MISSING" >&2
        exit 4
    fi
    if nm -gU "$XV_DIR/libhxlayer_xverify.dylib" | grep -q '_hxlayer_version'; then
        echo "  hxlayer_version SYMBOL=PRESENT"
    else
        echo "  hxlayer_version SYMBOL=MISSING" >&2
        exit 5
    fi
    echo "── xverify output ──"
    ls -la "$XV_DIR"/libhxlayer_xverify.dylib
    echo "done — Linux source compiles + links on Mac; ready to push"
    exit 0
fi

# ────────────────────────────────────────────────────────────────
# --mac-native : build libhxlayer.dylib on Mac for live FFI loading
# by ./hexa (interpreter) from ~/Dev/hexa-lang. Deliberately separate
# from --mac-xverify (which goes into xverify/ subdir and only exists
# to prove the source links). C2 Step 2 uses this artefact to exercise
# T5 (set HXLAYER_FFI_LIVE=1). The output file name matches the Linux
# naming (libhxlayer.dylib ↔ libhxlayer.so) so runtime.c's dlopen
# fallback (hexa_ffi_extract_libname) resolves @link("hxlayer")
# identically on both platforms.
# ────────────────────────────────────────────────────────────────
if [ "$MODE" = "--mac-native" ]; then
    UNAME=$(uname -s)
    if [ "$UNAME" != "Darwin" ]; then
        echo "--mac-native requires macOS, got $UNAME" >&2
        exit 2
    fi
    CC_M="${CC:-clang}"
    echo "[mac-native] building libhxlayer.dylib"
    # -D__linux__=1 flips the conditional in hxlayer_linux.c on —
    # the kernel only uses <math.h> and <stdint.h>, both present on
    # Mac's libSystem, so it compiles and links unchanged.
    $CC_M -O3 -fPIC -dynamiclib -D__linux__=1 \
        "$SRC_DIR/hxlayer_linux.c" \
        -lm \
        -o "$OUT_DIR/libhxlayer.dylib"

    if nm -gU "$OUT_DIR/libhxlayer.dylib" | grep -q '_hxlayer_rmsnorm_silu'; then
        echo "  hxlayer_rmsnorm_silu SYMBOL=PRESENT"
    else
        echo "  hxlayer_rmsnorm_silu SYMBOL=MISSING" >&2
        exit 4
    fi
    echo "── mac-native output ──"
    ls -la "$OUT_DIR/libhxlayer.dylib"
    echo "done — load with DYLD_LIBRARY_PATH=$OUT_DIR"
    exit 0
fi

# ────────────────────────────────────────────────────────────────
# Normal build mode — Linux only.
# ────────────────────────────────────────────────────────────────
UNAME=$(uname -s)
if [ "$UNAME" != "Linux" ]; then
    echo "build_hxlayer_linux.sh: expected Linux for normal build, got $UNAME" >&2
    echo "  - On Linux:  bash scripts/build_hxlayer_linux.sh" >&2
    echo "  - On Mac:    bash scripts/build_hxlayer_linux.sh --mac-xverify" >&2
    echo "  - Mac live FFI:  bash scripts/build_hxlayer_linux.sh --mac-native" >&2
    exit 2
fi

CFLAGS_BASE="-O3 -fPIC -shared -march=native"
if [ "$MODE" = "--debug" ]; then
    CFLAGS_BASE="-O0 -g -fPIC -shared"
fi

# ── libhxlayer.so — libm only (scalar O3 auto-vec) ──
echo "[1/1] building libhxlayer.so"
$CC $CFLAGS_BASE \
    "$SRC_DIR/hxlayer_linux.c" \
    -o "$OUT_DIR/libhxlayer.so" \
    -lm

echo "built:"
ls -la "$OUT_DIR"/libhxlayer.so

# ── Optional: deploy to system lib path ────────────────────────
if [ -n "${DEPLOY:-}" ]; then
    echo "deploying to $DEPLOY"
    cp "$OUT_DIR/libhxlayer.so" "$DEPLOY/"
    command -v ldconfig >/dev/null && ldconfig
fi

echo "done — set LD_LIBRARY_PATH=$OUT_DIR (or DEPLOY=/usr/local/lib)"
