#!/usr/bin/env bash
# build_hxblas_linux.sh — build libhxblas.so and libhxvdsp.so on Linux x86_64
#
# Port of the Mac build recipe from hxblas_wrapper.c/hxvdsp_wrapper.c
# (Accelerate-based) to Linux using OpenBLAS + libm. Same output names
# so hexa's @link("libhxblas") / @link("libhxvdsp") resolve identically
# on both platforms (dylib on Mac, .so on Linux).
#
# Prerequisites (Ubuntu/Debian):
#   apt install gcc libopenblas-dev libgomp1   # libomp included in libgomp
#
# Usage:
#   bash scripts/build_hxblas_linux.sh                   # normal build
#   bash scripts/build_hxblas_linux.sh --debug           # -O0 -g for debug
#   CC=clang bash scripts/build_hxblas_linux.sh          # use clang instead
#   DEPLOY=/usr/local/lib bash scripts/build_hxblas_linux.sh  # also copy to /usr/local/lib
#
# Outputs:
#   self/native/build/libhxblas.so
#   self/native/build/libhxvdsp.so
#
# On H100 pod this is run once per pod boot; binaries land in the
# /workspace/hexa-lang checkout. hexa @link("libhxblas") then resolves
# via LD_LIBRARY_PATH=/workspace/hexa-lang/self/native/build (or by
# copying into /usr/local/lib + ldconfig).

set -e

HEXA_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$HEXA_DIR/self/native"
OUT_DIR="$HEXA_DIR/self/native/build"
CC="${CC:-gcc}"

mkdir -p "$OUT_DIR"

# Platform sanity — this script is Linux-only.
UNAME=$(uname -s)
if [ "$UNAME" != "Linux" ]; then
    echo "build_hxblas_linux.sh: expected Linux, got $UNAME" >&2
    echo "  On macOS, the Mac wrappers live at anima/training/hxblas_wrapper.c" >&2
    echo "  Build command is in that .c file's header." >&2
    exit 2
fi

CFLAGS_BASE="-O3 -fPIC -shared -march=native"
if [ "${1:-}" = "--debug" ]; then
    CFLAGS_BASE="-O0 -g -fPIC -shared"
fi

# ── libhxblas.so — depends on libopenblas ─────────────────────
echo "[1/2] building libhxblas.so"
$CC $CFLAGS_BASE -fopenmp \
    "$SRC_DIR/hxblas_linux.c" \
    -o "$OUT_DIR/libhxblas.so" \
    -lopenblas -lgomp -lm

# ── libhxvdsp.so — libm only (scalar+O3 auto-vec) ─────────────
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

echo "done — set LD_LIBRARY_PATH=$OUT_DIR (or deploy with DEPLOY=/usr/local/lib)"
