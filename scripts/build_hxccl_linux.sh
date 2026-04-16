#!/usr/bin/env bash
# build_hxccl_linux.sh — build libhxccl.so on Linux (NCCL+CUDA backend),
# OR build a stub libhxccl_stub.dylib on Mac where CUDA/NCCL is absent.
#
# C9 roadmap — "ZeRO-3 / FSDP real multi-GPU" Day-1 shim. Companion to
# scripts/build_hxblas_linux.sh, scripts/build_hxlayer_linux.sh, and
# scripts/build_hxflash_linux.sh. Same flags, same layout, same
# --mac-* contract where practical.
#
# Prerequisites (Linux — runpod/pytorch:2.4.0 image or equivalent H100 pod):
#   apt install gcc
#   # CUDA 12 + NCCL 2 usually already present in the pytorch image:
#   ls /usr/local/cuda/lib64/libcudart.so
#   ls /usr/local/cuda/lib64/libnccl.so          # or /usr/lib/x86_64-linux-gnu
#
# Prerequisites (Mac stub mode):
#   (none — stub uses only libSystem)
#
# Usage:
#   bash scripts/build_hxccl_linux.sh                 # Linux Day-1: stub .so
#                                                     #   (single-rank memcpy,
#                                                     #    no libnccl required)
#   bash scripts/build_hxccl_linux.sh --with-nccl     # Linux Day-2: real NCCL
#                                                     #   needs libnccl + CUDA
#   bash scripts/build_hxccl_linux.sh --debug         # Linux: -O0 -g
#   bash scripts/build_hxccl_linux.sh --mac-stub      # Mac: stub .dylib
#                                                     #   (Family B → -1 on
#                                                     #    world>1, 0 on world=1)
#   CC=clang bash scripts/build_hxccl_linux.sh        # override compiler
#   CUDA_HOME=/opt/cuda bash scripts/build_hxccl_linux.sh --with-nccl
#   NCCL_HOME=/opt/nccl bash scripts/build_hxccl_linux.sh --with-nccl
#   DEPLOY=/usr/local/lib bash scripts/build_hxccl_linux.sh  # install
#
# Outputs (Linux mode):
#   self/native/build/libhxccl.so          (HXCCL_ENABLE_NCCL on)
#
# Outputs (--mac-stub mode):
#   self/native/build/libhxccl_stub.dylib  (no CUDA/NCCL, returns -1)
#
# H100 pod deployment:
#   bash scripts/build_hxccl_linux.sh
#   export LD_LIBRARY_PATH=$PWD/self/native/build:/usr/local/cuda/lib64:$LD_LIBRARY_PATH
#   export HXCCL_UNIQUE_ID_FILE=/tmp/hxccl_uid_$(uuidgen).bin  # per-job
#   # launch 1 process per rank, each setting HXCCL_RANK + HXCCL_WORLD_SIZE
#
# Multi-GPU launch pattern (H100 2-GPU pod example):
#   # rank 0
#   CUDA_VISIBLE_DEVICES=0 HXCCL_RANK=0 HXCCL_WORLD_SIZE=2 \
#       ./hexa self/test_hxccl_multigpu.hexa &
#   # rank 1
#   CUDA_VISIBLE_DEVICES=1 HXCCL_RANK=1 HXCCL_WORLD_SIZE=2 \
#       ./hexa self/test_hxccl_multigpu.hexa &
#   wait

set -e

HEXA_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$HEXA_DIR/self/native"
OUT_DIR="$HEXA_DIR/self/native/build"
CC="${CC:-gcc}"
MODE="${1:-build}"

mkdir -p "$OUT_DIR"

# ────────────────────────────────────────────────────────────────
# --mac-stub : build a stub libhxccl_stub.dylib on Mac. The same C
# source compiles without HXCCL_ENABLE_NCCL defined, taking the stub
# branch (init returns a non-null handle; collectives return -1).
# Lets Mac CI load the library via dlopen, exercise the call graph,
# and catch ABI drift before the Linux deploy.
# ────────────────────────────────────────────────────────────────
if [ "$MODE" = "--mac-stub" ]; then
    UNAME=$(uname -s)
    if [ "$UNAME" != "Darwin" ]; then
        echo "--mac-stub requires macOS, got $UNAME" >&2
        exit 2
    fi
    CC_M="${CC:-clang}"
    echo "[mac-stub] hxccl stub only on Mac, skipping real NCCL build"
    echo "[mac-stub] building libhxccl_stub.dylib (collectives return -1)"
    $CC_M -O3 -fPIC -dynamiclib \
        "$SRC_DIR/hxccl_linux.c" \
        -o "$OUT_DIR/libhxccl_stub.dylib"

    # Sanity: every Day-1 symbol must be exported.
    MISSING=0
    for sym in _hxccl_version _hxccl_init _hxccl_finalize \
               _hxccl_allreduce_float _hxccl_broadcast_float; do
        if nm -gU "$OUT_DIR/libhxccl_stub.dylib" | grep -q "$sym"; then
            echo "  $sym SYMBOL=PRESENT"
        else
            echo "  $sym SYMBOL=MISSING" >&2
            MISSING=1
        fi
    done
    if [ $MISSING -ne 0 ]; then exit 4; fi

    echo "── mac-stub output ──"
    ls -la "$OUT_DIR/libhxccl_stub.dylib"
    echo "done — stub ready. Use DYLD_LIBRARY_PATH=$OUT_DIR for smoke tests."
    exit 0
fi

# ────────────────────────────────────────────────────────────────
# Normal build mode — Linux.
#
# Day-1: stub-only — no -lnccl -lcudart required. The C source's
#   #ifdef HXCCL_NCCL blocks are dormant, so every collective still
#   behaves as a single-rank memcpy/no-op. This lets the shim
#   build cleanly on any Linux x86_64 (no CUDA pod required for CI).
#
# Day-2: pass --with-nccl to flip on -DHXCCL_NCCL=1 and link against
#   libnccl + libcudart. Requires CUDA_HOME (default /usr/local/cuda)
#   and a reachable libnccl.so.
# ────────────────────────────────────────────────────────────────
UNAME=$(uname -s)
if [ "$UNAME" != "Linux" ]; then
    echo "build_hxccl_linux.sh: expected Linux for real build, got $UNAME" >&2
    echo "  - On Linux:  bash scripts/build_hxccl_linux.sh" >&2
    echo "  - On Mac:    bash scripts/build_hxccl_linux.sh --mac-stub" >&2
    exit 2
fi

WITH_NCCL=0
for arg in "$@"; do
    if [ "$arg" = "--with-nccl" ]; then WITH_NCCL=1; fi
done

CFLAGS_BASE="-O3 -fPIC -shared -march=native"
if [ "$MODE" = "--debug" ]; then
    CFLAGS_BASE="-O0 -g -fPIC -shared"
fi

if [ $WITH_NCCL -eq 1 ]; then
    CUDA_HOME="${CUDA_HOME:-/usr/local/cuda}"
    NCCL_HOME="${NCCL_HOME:-$CUDA_HOME}"

    if [ ! -f "$CUDA_HOME/include/cuda_runtime.h" ]; then
        echo "ERROR: cuda_runtime.h not found at $CUDA_HOME/include" >&2
        echo "       set CUDA_HOME=/path/to/cuda" >&2
        exit 3
    fi

    NCCL_INC=""
    for p in "$NCCL_HOME/include/nccl.h" "/usr/include/nccl.h" \
             "/usr/local/include/nccl.h"; do
        if [ -f "$p" ]; then NCCL_INC=$(dirname "$p"); break; fi
    done
    if [ -z "$NCCL_INC" ]; then
        echo "ERROR: nccl.h not found (searched $NCCL_HOME/include, /usr/include, /usr/local/include)" >&2
        echo "       set NCCL_HOME=/path/to/nccl" >&2
        exit 3
    fi

    echo "[1/1] building libhxccl.so (Day-2: NCCL + CUDA runtime)"
    echo "  CUDA_HOME=$CUDA_HOME  NCCL_INC=$NCCL_INC"
    $CC $CFLAGS_BASE \
        -DHXCCL_NCCL=1 \
        -I"$CUDA_HOME/include" \
        -I"$NCCL_INC" \
        "$SRC_DIR/hxccl_linux.c" \
        -o "$OUT_DIR/libhxccl.so" \
        -L"$CUDA_HOME/lib64" \
        -L"$NCCL_HOME/lib" \
        -lnccl -lcudart
else
    echo "[1/1] building libhxccl.so (Day-1: stub, no NCCL/CUDA link)"
    $CC $CFLAGS_BASE \
        "$SRC_DIR/hxccl_linux.c" \
        -o "$OUT_DIR/libhxccl.so"
fi

echo "built:"
ls -la "$OUT_DIR"/libhxccl.so

# ── Optional: deploy to system lib path ────────────────────────
if [ -n "${DEPLOY:-}" ]; then
    echo "deploying to $DEPLOY"
    cp "$OUT_DIR/libhxccl.so" "$DEPLOY/"
    command -v ldconfig >/dev/null && ldconfig
fi

if [ $WITH_NCCL -eq 1 ]; then
    echo "done — set LD_LIBRARY_PATH=$OUT_DIR:$CUDA_HOME/lib64 (or DEPLOY=/usr/local/lib)"
else
    echo "done — set LD_LIBRARY_PATH=$OUT_DIR (Day-1 stub, no CUDA/NCCL needed)"
fi
