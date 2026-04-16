#!/usr/bin/env bash
# build_hxblas_linux.sh — build libhxblas.so + libhxvdsp.so on Linux,
# OR run a syntactic/link cross-verify on Mac before deploying to
# the H100 pod.
#
# Port of the Mac build recipe from hxblas_wrapper.c/hxvdsp_wrapper.c
# (Accelerate-based) to Linux using OpenBLAS + libm. Same exported
# symbol set so hexa's @link("hxblas") / @link("hxvdsp") resolves to
# the same ABI on both platforms (dylib on Mac, .so on Linux).
#
# Prerequisites (Ubuntu/Debian, runpod/pytorch:2.4.0 image):
#   apt install gcc libopenblas-dev libgomp1   # libomp ships with libgomp
#
# Prerequisites (Mac cross-verify mode):
#   brew install openblas                       # provides cblas.h
#
# Usage:
#   bash scripts/build_hxblas_linux.sh                   # normal Linux build
#   bash scripts/build_hxblas_linux.sh --debug           # -O0 -g for debug
#   bash scripts/build_hxblas_linux.sh --mac-xverify     # syntactic + link
#                                                        #   verify on Mac,
#                                                        #   compiles the Linux
#                                                        #   source as a .dylib
#                                                        #   linked against
#                                                        #   Homebrew OpenBLAS
#                                                        #   (no actual deploy)
#   CC=clang bash scripts/build_hxblas_linux.sh          # use clang instead
#   DEPLOY=/usr/local/lib bash scripts/build_hxblas_linux.sh  # copy to /usr/local/lib
#
# Outputs (Linux mode):
#   self/native/build/libhxblas.so
#   self/native/build/libhxvdsp.so
#
# Outputs (--mac-xverify mode):
#   self/native/build/xverify/libhxblas_xverify.dylib
#   self/native/build/xverify/libhxvdsp_xverify.dylib
#   plus a symbol-set diff against anima/training/build/libhx*.dylib
#   (existing Mac production artifacts) — must show ABI_DIFF=NONE.
#
# H100 pod deployment (Linux mode, after `git pull` on the pod):
#   bash scripts/build_hxblas_linux.sh                                   # build
#   export LD_LIBRARY_PATH=$PWD/self/native/build:$LD_LIBRARY_PATH      # use
#   # OR  bash scripts/build_hxblas_linux.sh && \
#   #       sudo install self/native/build/libhx*.so /usr/local/lib && \
#   #       sudo ldconfig                                               # install

set -e

HEXA_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$HEXA_DIR/self/native"
OUT_DIR="$HEXA_DIR/self/native/build"
CC="${CC:-gcc}"
MODE="${1:-build}"

mkdir -p "$OUT_DIR"

# ────────────────────────────────────────────────────────────────
# --mac-xverify : run on Mac to validate the Linux source compiles
# and links cleanly against OpenBLAS BEFORE committing or pushing to
# the H100 pod. Uses Homebrew OpenBLAS for the cblas headers and
# -D__linux__=1 to enable the gated #ifdef block.
# ────────────────────────────────────────────────────────────────
if [ "$MODE" = "--mac-xverify" ]; then
    UNAME=$(uname -s)
    if [ "$UNAME" != "Darwin" ]; then
        echo "--mac-xverify requires macOS, got $UNAME" >&2
        exit 2
    fi
    OBS_INC="${OPENBLAS_INC:-/opt/homebrew/opt/openblas/include}"
    OBS_LIB="${OPENBLAS_LIB:-/opt/homebrew/opt/openblas/lib}"
    if [ ! -f "$OBS_INC/cblas.h" ]; then
        echo "Homebrew openblas not found; install with: brew install openblas" >&2
        exit 3
    fi
    XV_DIR="$OUT_DIR/xverify"
    mkdir -p "$XV_DIR"
    # Force clang on Mac (gcc on Mac is just clang anyway, but be explicit)
    CC_X="${CC:-clang}"
    echo "[xverify 1/2] linking hxblas_linux.c → libhxblas_xverify.dylib"
    $CC_X -O3 -fPIC -dynamiclib -D__linux__=1 \
        -I"$OBS_INC" \
        "$SRC_DIR/hxblas_linux.c" \
        -L"$OBS_LIB" -lopenblas -lm \
        -o "$XV_DIR/libhxblas_xverify.dylib"
    echo "[xverify 2/2] linking hxvdsp_linux.c → libhxvdsp_xverify.dylib"
    $CC_X -O3 -fPIC -dynamiclib -D__linux__=1 \
        "$SRC_DIR/hxvdsp_linux.c" \
        -lm \
        -o "$XV_DIR/libhxvdsp_xverify.dylib"

    # Diff the exported symbol set against the existing Mac production
    # dylibs at anima/training/build/. ABI_DIFF=NONE means the Linux
    # binding will resolve every Hexa-side extern fn that the Mac
    # binding currently does.
    MAC_BLAS="${MAC_BLAS:-/Users/ghost/Dev/anima/training/build/libhxblas.dylib}"
    MAC_VDSP="${MAC_VDSP:-/Users/ghost/Dev/anima/training/build/libhxvdsp.dylib}"
    echo "── ABI parity check (vs $MAC_BLAS) ──"
    if [ -f "$MAC_BLAS" ]; then
        nm -gU "$MAC_BLAS"                       | awk '/_hxblas_/{print $3}' | sort > "$XV_DIR/mac_hxblas.txt"
        nm -gU "$XV_DIR/libhxblas_xverify.dylib" | awk '/_hxblas_/{print $3}' | sort > "$XV_DIR/lin_hxblas.txt"
        if diff -q "$XV_DIR/mac_hxblas.txt" "$XV_DIR/lin_hxblas.txt" >/dev/null; then
            echo "  hxblas ABI_DIFF=NONE ($(wc -l < "$XV_DIR/lin_hxblas.txt" | tr -d ' ') symbols)"
        else
            echo "  hxblas ABI_DIFF=MISMATCH (see $XV_DIR/{mac,lin}_hxblas.txt)" >&2
            diff "$XV_DIR/mac_hxblas.txt" "$XV_DIR/lin_hxblas.txt" || true
            exit 4
        fi
    fi
    echo "── ABI parity check (vs $MAC_VDSP) ──"
    if [ -f "$MAC_VDSP" ]; then
        nm -gU "$MAC_VDSP"                       | awk '/_hxvdsp_/{print $3}' | sort > "$XV_DIR/mac_hxvdsp.txt"
        nm -gU "$XV_DIR/libhxvdsp_xverify.dylib" | awk '/_hxvdsp_/{print $3}' | sort > "$XV_DIR/lin_hxvdsp.txt"
        if diff -q "$XV_DIR/mac_hxvdsp.txt" "$XV_DIR/lin_hxvdsp.txt" >/dev/null; then
            echo "  hxvdsp ABI_DIFF=NONE ($(wc -l < "$XV_DIR/lin_hxvdsp.txt" | tr -d ' ') symbols)"
        else
            echo "  hxvdsp ABI_DIFF=MISMATCH (see $XV_DIR/{mac,lin}_hxvdsp.txt)" >&2
            diff "$XV_DIR/mac_hxvdsp.txt" "$XV_DIR/lin_hxvdsp.txt" || true
            exit 5
        fi
    fi
    echo "── xverify outputs ──"
    ls -la "$XV_DIR"/libhx*_xverify.dylib
    echo "done — Linux source compiles + links + matches Mac ABI exactly"
    exit 0
fi

# ────────────────────────────────────────────────────────────────
# Normal build mode — Linux only.
# ────────────────────────────────────────────────────────────────
UNAME=$(uname -s)
if [ "$UNAME" != "Linux" ]; then
    echo "build_hxblas_linux.sh: expected Linux for normal build, got $UNAME" >&2
    echo "  - On Linux:  bash scripts/build_hxblas_linux.sh" >&2
    echo "  - On Mac:    bash scripts/build_hxblas_linux.sh --mac-xverify" >&2
    echo "  - Mac wrappers live at anima/training/hxblas_wrapper.c" >&2
    exit 2
fi

CFLAGS_BASE="-O3 -fPIC -shared -march=native"
if [ "$MODE" = "--debug" ]; then
    CFLAGS_BASE="-O0 -g -fPIC -shared"
fi

# ── libhxblas.so — depends on libopenblas + libgomp + libm ────
echo "[1/2] building libhxblas.so"
$CC $CFLAGS_BASE -fopenmp \
    "$SRC_DIR/hxblas_linux.c" \
    -o "$OUT_DIR/libhxblas.so" \
    -lopenblas -lgomp -lm

# ── libhxvdsp.so — libm only (scalar + O3 + -march=native auto-vec) ──
echo "[2/2] building libhxvdsp.so"
$CC $CFLAGS_BASE \
    "$SRC_DIR/hxvdsp_linux.c" \
    -o "$OUT_DIR/libhxvdsp.so" \
    -lm

echo "built:"
ls -la "$OUT_DIR"/libhx*.so

# ── Optional: deploy to system lib path ────────────────────────
if [ -n "${DEPLOY:-}" ]; then
    echo "deploying to $DEPLOY"
    cp "$OUT_DIR/libhxblas.so"  "$DEPLOY/"
    cp "$OUT_DIR/libhxvdsp.so"  "$DEPLOY/"
    command -v ldconfig >/dev/null && ldconfig
fi

echo "done — set LD_LIBRARY_PATH=$OUT_DIR (or DEPLOY=/usr/local/lib)"
