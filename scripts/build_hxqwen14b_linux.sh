#!/usr/bin/env bash
# build_hxqwen14b_linux.sh — build libhxqwen14b.so on Linux (H100 pod)
# OR run a syntactic/link cross-verify on Mac before push.
#
# Companion to build_hxlmhead_linux.sh / build_hxflash_linux.sh /
# build_hxccl_linux.sh. Same flag layout, same --mac-xverify /
# --mac-native contract, so H100 operators do not have to learn a new
# recipe.
#
# v0 STATUS: SKELETON. Builds the stub shim against libm only (no CUDA
# linked by default) so the symbol surface can be verified end-to-end.
# Set HXQWEN14B_CUDA=1 to enable the real CUDA + cuBLAS path once
# Day-1 kernels are in place.
#
# Prerequisites (Linux stub build, runpod/pytorch:2.4.0 image):
#   apt install gcc                                 # any C compiler
#
# Prerequisites (Linux CUDA build, HXQWEN14B_CUDA=1):
#   apt install gcc
#   CUDA 11.8 toolkit at /usr/local/cuda-11.8       # cuBLAS + bf16 Tensor Core
#   (runpod pytorch:2.4.0 image already ships CUDA 11.8 + cuBLAS)
#
# Prerequisites (Mac cross-verify):
#   Xcode command line tools                       # clang + libm
#   (stub path — no CUDA on Mac, ever)
#
# Usage:
#   bash scripts/build_hxqwen14b_linux.sh                   # stub Linux build
#   bash scripts/build_hxqwen14b_linux.sh --debug           # -O0 -g stub
#   HXQWEN14B_CUDA=1 bash scripts/build_hxqwen14b_linux.sh  # real CUDA path (Day-1)
#   bash scripts/build_hxqwen14b_linux.sh --mac-xverify     # syntactic on Mac
#   bash scripts/build_hxqwen14b_linux.sh --mac-native      # Mac .dylib stub
#   CC=clang bash scripts/build_hxqwen14b_linux.sh          # swap compiler
#   DEPLOY=/usr/local/lib bash scripts/build_hxqwen14b_linux.sh  # install
#
# Outputs (Linux mode):
#   self/native/build/libhxqwen14b.so
#
# Outputs (--mac-xverify mode):
#   self/native/build/xverify/libhxqwen14b_xverify.dylib
#
# Outputs (--mac-native mode):
#   self/native/build/libhxqwen14b.dylib           # loadable on Mac for ABI tests
#
# H100 pod deployment (Linux CUDA mode, after `git pull` on the pod):
#   HXQWEN14B_CUDA=1 bash scripts/build_hxqwen14b_linux.sh
#   export LD_LIBRARY_PATH=$PWD/self/native/build:/usr/local/cuda/lib64:$LD_LIBRARY_PATH

set -e

HEXA_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$HEXA_DIR/self/native"
OUT_DIR="$HEXA_DIR/self/native/build"
CC="${CC:-gcc}"
MODE="${1:-build}"
CUDA_HOME="${CUDA_HOME:-/usr/local/cuda-11.8}"

mkdir -p "$OUT_DIR"

SYMBOLS="hxqwen14b_load hxqwen14b_generate hxqwen14b_compute_phi_holo hxqwen14b_free hxqwen14b_version"

# ────────────────────────────────────────────────────────────────
# --mac-xverify : compile on Mac to validate the Linux source is
# syntactically clean BEFORE committing or pushing to the H100 pod.
# Stub path only — no CUDA on Mac, so HXQWEN14B_CUDA is ignored here.
# ────────────────────────────────────────────────────────────────
if [ "$MODE" = "--mac-xverify" ]; then
    UNAME=$(uname -s)
    if [ "$UNAME" != "Darwin" ]; then
        echo "--mac-xverify requires macOS, got $UNAME" >&2
        exit 2
    fi
    XV_DIR="$OUT_DIR/xverify"
    mkdir -p "$XV_DIR"
    CC_X="${CC:-clang}"
    echo "[xverify] linking hxqwen14b.c → libhxqwen14b_xverify.dylib (stub, no CUDA)"
    $CC_X -O3 -fPIC -dynamiclib \
        "$SRC_DIR/hxqwen14b.c" \
        -lm \
        -o "$XV_DIR/libhxqwen14b_xverify.dylib"

    for sym in $SYMBOLS; do
        if nm -gU "$XV_DIR/libhxqwen14b_xverify.dylib" | grep -q "_$sym"; then
            echo "  $sym SYMBOL=PRESENT"
        else
            echo "  $sym SYMBOL=MISSING" >&2
            exit 4
        fi
    done
    echo "── xverify output ──"
    ls -la "$XV_DIR"/libhxqwen14b_xverify.dylib
    echo "done — Linux source compiles + links on Mac; ready to push"
    exit 0
fi

# ────────────────────────────────────────────────────────────────
# --mac-native : build libhxqwen14b.dylib on Mac for ABI smoke tests
# by ./hexa (interpreter) from ~/Dev/hexa-lang. Stub path only — no
# CUDA on Mac, so real generation always returns -4 here.
# ────────────────────────────────────────────────────────────────
if [ "$MODE" = "--mac-native" ]; then
    UNAME=$(uname -s)
    if [ "$UNAME" != "Darwin" ]; then
        echo "--mac-native requires macOS, got $UNAME" >&2
        exit 2
    fi
    CC_M="${CC:-clang}"
    echo "[mac-native] building libhxqwen14b.dylib (stub, no CUDA)"
    $CC_M -O3 -fPIC -dynamiclib \
        "$SRC_DIR/hxqwen14b.c" \
        -lm \
        -o "$OUT_DIR/libhxqwen14b.dylib"

    for sym in $SYMBOLS; do
        if nm -gU "$OUT_DIR/libhxqwen14b.dylib" | grep -q "_$sym"; then
            echo "  $sym SYMBOL=PRESENT"
        else
            echo "  $sym SYMBOL=MISSING" >&2
            exit 4
        fi
    done
    echo "── mac-native output ──"
    ls -la "$OUT_DIR/libhxqwen14b.dylib"
    echo "done — load with DYLD_LIBRARY_PATH=$OUT_DIR"
    exit 0
fi

# ────────────────────────────────────────────────────────────────
# Normal build mode — Linux only.
# ────────────────────────────────────────────────────────────────
UNAME=$(uname -s)
if [ "$UNAME" != "Linux" ]; then
    echo "build_hxqwen14b_linux.sh: expected Linux for normal build, got $UNAME" >&2
    echo "  - On Linux:        bash scripts/build_hxqwen14b_linux.sh" >&2
    echo "  - CUDA (H100):     HXQWEN14B_CUDA=1 bash scripts/build_hxqwen14b_linux.sh" >&2
    echo "  - On Mac xverify:  bash scripts/build_hxqwen14b_linux.sh --mac-xverify" >&2
    echo "  - Mac live stub:   bash scripts/build_hxqwen14b_linux.sh --mac-native" >&2
    exit 2
fi

CFLAGS_BASE="-O3 -fPIC -shared -march=native"
if [ "$MODE" = "--debug" ]; then
    CFLAGS_BASE="-O0 -g -fPIC -shared"
fi

# ── libhxqwen14b.so ─────────────────────────────────────────────
if [ "${HXQWEN14B_CUDA:-0}" = "1" ]; then
    # Day-1 real CUDA path — cuBLAS + bf16 Tensor Core.
    if [ ! -d "$CUDA_HOME" ]; then
        echo "[ERROR] HXQWEN14B_CUDA=1 but CUDA_HOME=$CUDA_HOME not found" >&2
        echo "  Override with CUDA_HOME=/path/to/cuda" >&2
        exit 3
    fi
    echo "[1/1] building libhxqwen14b.so (CUDA path — cuBLAS + bf16)"
    $CC $CFLAGS_BASE \
        -DHXQWEN14B_CUDA \
        -I"$CUDA_HOME/include" \
        "$SRC_DIR/hxqwen14b.c" \
        -o "$OUT_DIR/libhxqwen14b.so" \
        -L"$CUDA_HOME/lib64" -lcudart -lcublas -lm
else
    # v0 stub path — no CUDA linked. Symbol surface only.
    echo "[1/1] building libhxqwen14b.so (stub path — no CUDA)"
    $CC $CFLAGS_BASE \
        "$SRC_DIR/hxqwen14b.c" \
        -o "$OUT_DIR/libhxqwen14b.so" \
        -lm
fi

for sym in $SYMBOLS; do
    if nm -D "$OUT_DIR/libhxqwen14b.so" 2>/dev/null | grep -q " $sym"; then
        echo "  $sym SYMBOL=PRESENT"
    else
        echo "  $sym SYMBOL=MISSING (nm -D failed; check ldd/readelf)" >&2
    fi
done

echo "built:"
ls -la "$OUT_DIR"/libhxqwen14b.so

# ── Optional: deploy to system lib path ─────────────────────────
if [ -n "${DEPLOY:-}" ]; then
    echo "deploying to $DEPLOY"
    cp "$OUT_DIR/libhxqwen14b.so" "$DEPLOY/"
    command -v ldconfig >/dev/null && ldconfig
fi

echo "done — set LD_LIBRARY_PATH=$OUT_DIR (or DEPLOY=/usr/local/lib)"
