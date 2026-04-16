#!/usr/bin/env bash
# build_hxcuda_linux.sh -- build libhxcuda.so on Linux (H100, sm_90)
#
# C4 roadmap -- "CUDA fused mega-kernels + bf16 Tensor Core"
# Companion to build_hxblas_linux.sh, build_hxflash_linux.sh.
# Same layout and operator contract.
#
# Prerequisites (Linux, CUDA 12.x):
#   - nvcc in PATH (CUDA toolkit)
#   - GPU with sm_90 (H100) for bf16 WMMA -- degrades gracefully
#     on sm_80 (A100) if --arch is overridden.
#
# Usage:
#   bash scripts/build_hxcuda_linux.sh                # normal build (sm_90)
#   bash scripts/build_hxcuda_linux.sh --debug        # -O0 -G for CUDA debug
#   bash scripts/build_hxcuda_linux.sh --arch=sm_80   # override arch (A100)
#   bash scripts/build_hxcuda_linux.sh --syntax-check # compile-only, no link
#   DEPLOY=/usr/local/lib bash scripts/build_hxcuda_linux.sh  # install
#
# Outputs:
#   self/native/build/libhxcuda.so
#
# H100 pod deployment (after git pull):
#   bash scripts/build_hxcuda_linux.sh
#   export LD_LIBRARY_PATH=$PWD/self/native/build:$LD_LIBRARY_PATH

set -e

HEXA_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$HEXA_DIR/self/native"
OUT_DIR="$HEXA_DIR/self/native/build"
SRC="$SRC_DIR/hxcuda_fused.cu"

mkdir -p "$OUT_DIR"

# ── Parse args ────────────────────────────────────────────────────
ARCH="sm_90"
OPT="-O2"
MODE="build"

for arg in "$@"; do
    case "$arg" in
        --debug)
            OPT="-O0 -G"
            ;;
        --arch=*)
            ARCH="${arg#--arch=}"
            ;;
        --syntax-check)
            MODE="syntax"
            ;;
        *)
            echo "unknown arg: $arg" >&2
            exit 1
            ;;
    esac
done

# ── Verify nvcc ───────────────────────────────────────────────────
if ! command -v nvcc >/dev/null 2>&1; then
    echo "[build_hxcuda] ERROR: nvcc not found in PATH" >&2
    echo "  Install CUDA toolkit or add /usr/local/cuda/bin to PATH" >&2
    exit 2
fi

NVCC_VER=$(nvcc --version 2>&1 | grep -oP 'release \K[0-9.]+' || echo "unknown")

# Auto-detect GPU arch if not overridden via --arch
if [ "$ARCH" = "sm_90" ]; then
    GPU_NAME=$(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -1 || true)
    case "$GPU_NAME" in
        *H100*) ARCH="sm_90" ;;
        *A100*) ARCH="sm_80" ;;
        *4090*|*4080*) ARCH="sm_89" ;;
        *3090*|*3080*) ARCH="sm_86" ;;
        *) ;; # keep default sm_90
    esac
fi
echo "[build_hxcuda] nvcc version: $NVCC_VER, target arch: $ARCH, gpu: ${GPU_NAME:-unknown}"

# ── Syntax-check only ────────────────────────────────────────────
if [ "$MODE" = "syntax" ]; then
    echo "[build_hxcuda] syntax check: $SRC"
    nvcc -arch="$ARCH" $OPT --ptx \
        "$SRC" \
        -o /dev/null 2>&1
    echo "[build_hxcuda] syntax check PASSED"
    exit 0
fi

# ── Build libhxcuda.so ───────────────────────────────────────────
echo "[build_hxcuda] compiling $SRC -> libhxcuda.so (arch=$ARCH)"

nvcc -arch="$ARCH" $OPT \
    --shared -Xcompiler -fPIC \
    "$SRC" \
    -o "$OUT_DIR/libhxcuda.so"

echo "[build_hxcuda] built:"
ls -la "$OUT_DIR/libhxcuda.so"

# ── Symbol verification ──────────────────────────────────────────
echo "[build_hxcuda] symbol check:"

EXPECTED_SYMS="hxcuda_version hxcuda_matmul_bf16 hxcuda_matmul_bf16_pos hxcuda_fused_lmhead_fwd hxcuda_device_info hxcuda_sync"
ALL_OK=1
for sym in $EXPECTED_SYMS; do
    if nm -D "$OUT_DIR/libhxcuda.so" 2>/dev/null | grep -q " T $sym\|_$sym"; then
        echo "  $sym PRESENT"
    else
        echo "  $sym MISSING" >&2
        ALL_OK=0
    fi
done

if [ "$ALL_OK" -ne 1 ]; then
    echo "[build_hxcuda] WARNING: some symbols missing (may still work via dlsym)" >&2
fi

# ── Optional: deploy to system lib path ──────────────────────────
if [ -n "${DEPLOY:-}" ]; then
    echo "[build_hxcuda] deploying to $DEPLOY"
    cp "$OUT_DIR/libhxcuda.so" "$DEPLOY/"
    command -v ldconfig >/dev/null && ldconfig
fi

echo "[build_hxcuda] done -- set LD_LIBRARY_PATH=$OUT_DIR"
