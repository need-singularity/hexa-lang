// lora_cuda.h — F-CUDA-LORA-1 public ABI for the custom LoRA fwd/bwd kernels.
//
// This header is intentionally C-clean (extern "C" wrapped) so it can be
// included from either gcc-compiled host code (lora_cuda_host.c) or directly
// from nvcc-compiled lora_cuda.cu. The header has zero CUDA dependencies so
// it is safe to include on Mac (no CUDA toolkit) builds.
//
// Math contract (row-major, fp32):
//   Forward:  y[M,N] = (apply_base ? x·W : 0) + α · (x·A) · B
//   Backward: u = α·dy·Bᵀ ; dA = xᵀ·u ; dB = α·(x·A)ᵀ·dy ; dx += u·Aᵀ
//
// Default rank R = 64 (matches the F-CUDA-LORA-1 acceptance test).

#ifndef LORA_CUDA_H
#define LORA_CUDA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Return codes (stable; do not renumber).
#define LORA_RC_OK                  0
#define LORA_RC_BAD_ARGS           -2
#define LORA_RC_KERNEL_FAIL        -4
#define LORA_RC_DTYPE_TODO         -7   // bf16 / fp16 not yet implemented
#define LORA_RC_NO_CUDA_RUNTIME    -8   // built without nvcc; device path unreachable

// Forward args. All pointers are device pointers when calling
// lora_cuda_forward_launch and host pointers when calling
// lora_cpu_forward_reference. The same struct is reused so the test harness
// can A/B the two paths with the same call site.
typedef struct {
    const void*  x;            // [M, K]   row-major, fp32 (or dtype_id-typed)
    const void*  W;            // [K, N]   frozen base weight; may be NULL when apply_base=0
    const void*  A;            // [K, R]   LoRA down-projection
    const void*  B;            // [R, N]   LoRA up-projection
    void*        y;            // [M, N]   output
    void*        tmp;          // [M, R]   scratch (caller-owned)
    int32_t      M;
    int32_t      N;
    int32_t      K;
    int32_t      R;            // rank (default 64)
    float        alpha;        // α/r already folded in by caller (so e.g. α=16, r=64 → pass 16/64)
    int32_t      apply_base;   // 1 = include x·W ; 0 = LoRA-delta only (caller-managed base)
    int32_t      dtype_id;     // 0=fp32 (only one implemented), 1=bf16, 2=fp16
    int32_t      reserved0;
} lora_fwd_args_t;

typedef struct {
    const void*  x;            // [M, K]
    const void*  A;            // [K, R]
    const void*  B;            // [R, N]
    const void*  dy;           // [M, N]   incoming gradient
    void*        dA;           // [K, R]
    void*        dB;           // [R, N]
    void*        dx;           // [M, K]   may be NULL — if non-NULL, accumulates LoRA dx
    void*        u;            // [M, R]   scratch
    void*        tmp;          // [M, R]   scratch (recomputed = x·A)
    int32_t      M;
    int32_t      N;
    int32_t      K;
    int32_t      R;
    float        alpha;
    int32_t      accumulate_grads;   // 0=overwrite dA/dB ; 1=accumulate (gradient accumulation step)
    int32_t      dtype_id;
    int32_t      reserved0;
} lora_bwd_args_t;

// ─── CUDA path (compiled into libhxqwen14b.so when HXQWEN14B_CUDA=1) ───
// Returns LORA_RC_NO_CUDA_RUNTIME=-8 from the stub when the .so was built
// without nvcc; returns LORA_RC_OK / LORA_RC_KERNEL_FAIL otherwise.
//
// `stream` is an opaque cudaStream_t (cast to void*); pass NULL for the
// default stream.
int lora_cuda_forward_launch(const lora_fwd_args_t* args, void* stream);
int lora_cuda_backward_launch(const lora_bwd_args_t* args, void* stream);
int lora_cuda_runtime_available(void);

// ─── CPU reference path (always available; used for numerical equivalence) ──
int lora_cpu_forward_reference (const lora_fwd_args_t* args);
int lora_cpu_backward_reference(const lora_bwd_args_t* args);

// Top-level dispatch — picks CUDA path if `is_device` is non-zero AND
// runtime is available, else CPU. Test harness uses this to drive both
// paths through one entry point.
int lora_forward_dispatch (const lora_fwd_args_t* args, int is_device, void* stream);
int lora_backward_dispatch(const lora_bwd_args_t* args, int is_device, void* stream);

#ifdef __cplusplus
}
#endif
#endif  // LORA_CUDA_H
