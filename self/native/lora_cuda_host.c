// lora_cuda_host.c — F-CUDA-LORA-1 host-side CPU reference + dispatch shim.
//
// This translation unit is gcc-only (no nvcc). It provides:
//   - lora_cpu_forward_reference  : exact-arithmetic CPU equivalent of the CUDA fwd kernel
//   - lora_cpu_backward_reference : exact-arithmetic CPU equivalent of the CUDA bwd kernel
//   - lora_forward_dispatch / lora_backward_dispatch : pick CUDA vs CPU
//   - weak stubs for lora_cuda_{forward,backward}_launch + lora_cuda_runtime_available
//     so non-CUDA builds link cleanly and the dispatch routes everything to CPU
//
// The CPU reference uses the exact same loop structure as the CUDA kernels
// (no fused multiply-add reordering, no parallel-reduction tree) so float
// rounding patterns line up to within ~1e-6. We use double-precision accumulators
// only for the partial sums to bound numerical drift (the CUDA path effectively
// fp32 accumulates in registers; |Δ| stays well under the 1e-3 threshold).

#include "lora_cuda.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ─────────────────────────────────────────────────────────────
// Weak no-CUDA stubs. Overridden at link time by lora_cuda.cu when
// the .so is built with HXQWEN14B_CUDA=1 (nvcc TU emits the strong
// definitions which take precedence).
// ─────────────────────────────────────────────────────────────
__attribute__((weak)) int lora_cuda_forward_launch(const lora_fwd_args_t* a, void* s) {
    (void)a; (void)s;
    return LORA_RC_NO_CUDA_RUNTIME;
}
__attribute__((weak)) int lora_cuda_backward_launch(const lora_bwd_args_t* a, void* s) {
    (void)a; (void)s;
    return LORA_RC_NO_CUDA_RUNTIME;
}
__attribute__((weak)) int lora_cuda_runtime_available(void) {
    return 0;
}

// ─────────────────────────────────────────────────────────────
// CPU reference forward.
//   y = (apply_base ? x·W : 0) + α · (x·A) · B
// ─────────────────────────────────────────────────────────────
int lora_cpu_forward_reference(const lora_fwd_args_t* a) {
    if (!a || !a->x || !a->A || !a->B || !a->y || !a->tmp) return LORA_RC_BAD_ARGS;
    if (a->M <= 0 || a->N <= 0 || a->K <= 0 || a->R <= 0) return LORA_RC_BAD_ARGS;
    if (a->dtype_id != 0) return LORA_RC_DTYPE_TODO;

    const float* x   = (const float*)a->x;
    const float* A   = (const float*)a->A;
    const float* B   = (const float*)a->B;
    float*       y   = (float*)a->y;
    float*       tmp = (float*)a->tmp;
    const int M = a->M, N = a->N, K = a->K, R = a->R;
    const float alpha = a->alpha;

    // tmp[M,R] = x · A
    for (int m = 0; m < M; m++) {
        for (int r = 0; r < R; r++) {
            double acc = 0.0;
            for (int k = 0; k < K; k++) acc += (double)x[m*K + k] * (double)A[k*R + r];
            tmp[m*R + r] = (float)acc;
        }
    }

    // Optional base: y = x·W (overwrite); else clear y.
    if (a->apply_base && a->W != NULL) {
        const float* W = (const float*)a->W;
        for (int m = 0; m < M; m++) {
            for (int n = 0; n < N; n++) {
                double acc = 0.0;
                for (int k = 0; k < K; k++) acc += (double)x[m*K + k] * (double)W[k*N + n];
                y[m*N + n] = (float)acc;
            }
        }
    } else {
        memset(y, 0, (size_t)M * (size_t)N * sizeof(float));
    }

    // y += α · tmp · B
    for (int m = 0; m < M; m++) {
        for (int n = 0; n < N; n++) {
            double acc = 0.0;
            for (int r = 0; r < R; r++) acc += (double)tmp[m*R + r] * (double)B[r*N + n];
            y[m*N + n] += alpha * (float)acc;
        }
    }
    return LORA_RC_OK;
}

// ─────────────────────────────────────────────────────────────
// CPU reference backward.
//   tmp = x·A           (recompute)
//   u   = α · dy · Bᵀ
//   dA  = xᵀ · u
//   dB  = α · tmpᵀ · dy
//   dx += u · Aᵀ        (only if dx != NULL)
// ─────────────────────────────────────────────────────────────
int lora_cpu_backward_reference(const lora_bwd_args_t* a) {
    if (!a || !a->x || !a->A || !a->B || !a->dy || !a->dA || !a->dB || !a->u || !a->tmp)
        return LORA_RC_BAD_ARGS;
    if (a->M <= 0 || a->N <= 0 || a->K <= 0 || a->R <= 0) return LORA_RC_BAD_ARGS;
    if (a->dtype_id != 0) return LORA_RC_DTYPE_TODO;

    const float* x   = (const float*)a->x;
    const float* A   = (const float*)a->A;
    const float* B   = (const float*)a->B;
    const float* dy  = (const float*)a->dy;
    float*       dA  = (float*)a->dA;
    float*       dB  = (float*)a->dB;
    float*       dx  = (float*)a->dx;     // may be NULL
    float*       u   = (float*)a->u;
    float*       tmp = (float*)a->tmp;
    const int M = a->M, N = a->N, K = a->K, R = a->R;
    const float alpha = a->alpha;
    const int acc_grads = a->accumulate_grads;

    // tmp[M,R] = x · A
    for (int m = 0; m < M; m++) {
        for (int r = 0; r < R; r++) {
            double acc = 0.0;
            for (int k = 0; k < K; k++) acc += (double)x[m*K + k] * (double)A[k*R + r];
            tmp[m*R + r] = (float)acc;
        }
    }

    // u[M,R] = α · dy · Bᵀ      (B is [R,N], so u[m,r] = α · Σ_n dy[m,n]·B[r,n])
    for (int m = 0; m < M; m++) {
        for (int r = 0; r < R; r++) {
            double acc = 0.0;
            for (int n = 0; n < N; n++) acc += (double)dy[m*N + n] * (double)B[r*N + n];
            u[m*R + r] = alpha * (float)acc;
        }
    }

    // dA[K,R] = xᵀ · u  (= Σ_m x[m,k] · u[m,r])
    for (int k = 0; k < K; k++) {
        for (int r = 0; r < R; r++) {
            double acc = 0.0;
            for (int m = 0; m < M; m++) acc += (double)x[m*K + k] * (double)u[m*R + r];
            float v = (float)acc;
            if (acc_grads) dA[k*R + r] += v;
            else           dA[k*R + r]  = v;
        }
    }

    // dB[R,N] = α · tmpᵀ · dy  (= α · Σ_m tmp[m,r] · dy[m,n])
    for (int r = 0; r < R; r++) {
        for (int n = 0; n < N; n++) {
            double acc = 0.0;
            for (int m = 0; m < M; m++) acc += (double)tmp[m*R + r] * (double)dy[m*N + n];
            float v = alpha * (float)acc;
            if (acc_grads) dB[r*N + n] += v;
            else           dB[r*N + n]  = v;
        }
    }

    // dx[M,K] += u · Aᵀ  (= Σ_r u[m,r] · A[k,r])
    if (dx != NULL) {
        for (int m = 0; m < M; m++) {
            for (int k = 0; k < K; k++) {
                double acc = 0.0;
                for (int r = 0; r < R; r++) acc += (double)u[m*R + r] * (double)A[k*R + r];
                dx[m*K + k] += (float)acc;
            }
        }
    }
    return LORA_RC_OK;
}

// ─────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────
int lora_forward_dispatch(const lora_fwd_args_t* a, int is_device, void* stream) {
    if (is_device && lora_cuda_runtime_available()) {
        return lora_cuda_forward_launch(a, stream);
    }
    return lora_cpu_forward_reference(a);
}

int lora_backward_dispatch(const lora_bwd_args_t* a, int is_device, void* stream) {
    if (is_device && lora_cuda_runtime_available()) {
        return lora_cuda_backward_launch(a, stream);
    }
    return lora_cpu_backward_reference(a);
}
