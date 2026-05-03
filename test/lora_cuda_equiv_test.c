// test/lora_cuda_equiv_test.c — F-CUDA-LORA-1 numerical equivalence harness.
//
// What this proves:
//   1) The CUDA-shaped lora_forward_dispatch / lora_backward_dispatch path
//      (which falls through to lora_cpu_*_reference on non-CUDA builds)
//      matches an independent closed-form CPU implementation written
//      directly from the math contract:
//          y    = x · W  +  α · (x · A) · B
//          u    = α · dy · Bᵀ
//          dA   = xᵀ · u
//          dB   = α · (x · A)ᵀ · dy
//          dx  += u · Aᵀ
//      to within |Δ| < 1e-3 absolute, on r=64 with M=8, N=K=128.
//
//   2) On a machine with the .so built via HXQWEN14B_CUDA=1 + CUDA available,
//      flipping `is_device=1` exercises the GPU launchers; the same |Δ|<1e-3
//      assertion holds. On Mac (no CUDA) `lora_cuda_runtime_available()`
//      returns 0 and the dispatcher falls through to the CPU reference,
//      keeping the test green.
//
// PENDING-HARDWARE: the F-CUDA-LORA-1 acceptance against PyTorch (rather
// than a self-CPU reference) requires the .so loaded inside a Python
// process on RTX 5070 / H100. That step is documented in
// doc/plans/cuda_lora_pending_hardware.md (see commit message). This test
// only certifies the kernel-vs-reference equivalence; it does NOT fabricate
// any GPU result.
//
// Build (Mac/Linux CPU):
//   cc -O2 -I self/native test/lora_cuda_equiv_test.c \
//      self/native/lora_cuda_host.c -lm -o test/lora_cuda_equiv_test
//   ./test/lora_cuda_equiv_test
//
// Build (Linux CUDA):
//   nvcc -O3 -arch=sm_80 -c self/native/lora_cuda.cu -o lora_cuda.o
//   cc -O2 -I self/native test/lora_cuda_equiv_test.c \
//      self/native/lora_cuda_host.c lora_cuda.o \
//      -L/usr/local/cuda/lib64 -lcudart -lm -o test/lora_cuda_equiv_test
//   ./test/lora_cuda_equiv_test --device

#include "../self/native/lora_cuda.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Deterministic PRNG — xorshift32 (no libc rand for portability).
static uint32_t g_rng = 0xdeadbeefu;
static float frand_unit(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    // Map to [-0.5, 0.5).
    return ((float)(g_rng & 0xffffffu) / (float)0x1000000u) - 0.5f;
}

static void fill_random(float* p, int n) {
    for (int i = 0; i < n; i++) p[i] = frand_unit();
}

// Independent closed-form reference (different loop order than lora_cpu_*).
static void closed_form_forward(
    const float* x, const float* W, const float* A, const float* B,
    float* y, int M, int N, int K, int R, float alpha, int apply_base
) {
    // y = (x·W)?  +  α·(x·A)·B  — computed as a triple Σ_k Σ_r without
    // intermediate, so any indexing/transpose bug in the kernel surfaces.
    for (int m = 0; m < M; m++) {
        for (int n = 0; n < N; n++) {
            double base = 0.0;
            if (apply_base && W) {
                for (int k = 0; k < K; k++) base += (double)x[m*K+k] * (double)W[k*N+n];
            }
            double lora = 0.0;
            for (int r = 0; r < R; r++) {
                double xa = 0.0;
                for (int k = 0; k < K; k++) xa += (double)x[m*K+k] * (double)A[k*R+r];
                lora += xa * (double)B[r*N+n];
            }
            y[m*N + n] = (float)(base + (double)alpha * lora);
        }
    }
}

static void closed_form_backward(
    const float* x, const float* A, const float* B, const float* dy,
    float* dA, float* dB, float* dx,
    int M, int N, int K, int R, float alpha
) {
    // dA[k,r] = α · Σ_m Σ_n x[m,k] · dy[m,n] · B[r,n]
    for (int k = 0; k < K; k++) {
        for (int r = 0; r < R; r++) {
            double acc = 0.0;
            for (int m = 0; m < M; m++) {
                double dyB = 0.0;
                for (int n = 0; n < N; n++) dyB += (double)dy[m*N+n] * (double)B[r*N+n];
                acc += (double)x[m*K+k] * dyB;
            }
            dA[k*R + r] = (float)((double)alpha * acc);
        }
    }
    // dB[r,n] = α · Σ_m Σ_k x[m,k] · A[k,r] · dy[m,n]
    for (int r = 0; r < R; r++) {
        for (int n = 0; n < N; n++) {
            double acc = 0.0;
            for (int m = 0; m < M; m++) {
                double xa = 0.0;
                for (int k = 0; k < K; k++) xa += (double)x[m*K+k] * (double)A[k*R+r];
                acc += xa * (double)dy[m*N+n];
            }
            dB[r*N + n] = (float)((double)alpha * acc);
        }
    }
    // dx[m,k] = α · Σ_r Σ_n dy[m,n] · B[r,n] · A[k,r]
    for (int m = 0; m < M; m++) {
        for (int k = 0; k < K; k++) {
            double acc = 0.0;
            for (int r = 0; r < R; r++) {
                double dyB = 0.0;
                for (int n = 0; n < N; n++) dyB += (double)dy[m*N+n] * (double)B[r*N+n];
                acc += dyB * (double)A[k*R+r];
            }
            dx[m*K + k] = (float)((double)alpha * acc);
        }
    }
}

static float max_abs_diff(const float* a, const float* b, int n) {
    float mx = 0.0f;
    for (int i = 0; i < n; i++) {
        float d = fabsf(a[i] - b[i]);
        if (d > mx) mx = d;
    }
    return mx;
}

// One-shot test harness.
static int run_one(int M, int N, int K, int R, float alpha, int apply_base, int is_device) {
    float* x   = (float*)malloc(sizeof(float) * (size_t)M * (size_t)K);
    float* W   = (float*)malloc(sizeof(float) * (size_t)K * (size_t)N);
    float* A   = (float*)malloc(sizeof(float) * (size_t)K * (size_t)R);
    float* B   = (float*)malloc(sizeof(float) * (size_t)R * (size_t)N);
    float* y_dispatch = (float*)malloc(sizeof(float) * (size_t)M * (size_t)N);
    float* y_ref      = (float*)malloc(sizeof(float) * (size_t)M * (size_t)N);
    float* tmp = (float*)malloc(sizeof(float) * (size_t)M * (size_t)R);
    float* u   = (float*)malloc(sizeof(float) * (size_t)M * (size_t)R);

    float* dy  = (float*)malloc(sizeof(float) * (size_t)M * (size_t)N);
    float* dA_dispatch = (float*)calloc((size_t)K * (size_t)R, sizeof(float));
    float* dB_dispatch = (float*)calloc((size_t)R * (size_t)N, sizeof(float));
    float* dx_dispatch = (float*)calloc((size_t)M * (size_t)K, sizeof(float));
    float* dA_ref = (float*)calloc((size_t)K * (size_t)R, sizeof(float));
    float* dB_ref = (float*)calloc((size_t)R * (size_t)N, sizeof(float));
    float* dx_ref = (float*)calloc((size_t)M * (size_t)K, sizeof(float));

    if (!x || !W || !A || !B || !y_dispatch || !y_ref || !tmp || !u ||
        !dy || !dA_dispatch || !dB_dispatch || !dx_dispatch ||
        !dA_ref || !dB_ref || !dx_ref) {
        fprintf(stderr, "OOM\n");
        return 1;
    }

    fill_random(x, M*K);
    fill_random(W, K*N);
    fill_random(A, K*R);
    fill_random(B, R*N);
    fill_random(dy, M*N);

    // ── Forward ────────────────────────────────────────────────
    lora_fwd_args_t fa = {0};
    fa.x = x; fa.W = W; fa.A = A; fa.B = B;
    fa.y = y_dispatch; fa.tmp = tmp;
    fa.M = M; fa.N = N; fa.K = K; fa.R = R;
    fa.alpha = alpha; fa.apply_base = apply_base; fa.dtype_id = 0;
    int rc_fwd = lora_forward_dispatch(&fa, is_device, NULL);
    if (rc_fwd != LORA_RC_OK) {
        fprintf(stderr, "  forward dispatch rc=%d\n", rc_fwd);
        return 1;
    }
    closed_form_forward(x, W, A, B, y_ref, M, N, K, R, alpha, apply_base);
    float dy_max = max_abs_diff(y_dispatch, y_ref, M*N);

    // ── Backward ───────────────────────────────────────────────
    lora_bwd_args_t ba = {0};
    ba.x = x; ba.A = A; ba.B = B; ba.dy = dy;
    ba.dA = dA_dispatch; ba.dB = dB_dispatch; ba.dx = dx_dispatch;
    ba.u = u; ba.tmp = tmp;
    ba.M = M; ba.N = N; ba.K = K; ba.R = R;
    ba.alpha = alpha; ba.accumulate_grads = 0; ba.dtype_id = 0;
    int rc_bwd = lora_backward_dispatch(&ba, is_device, NULL);
    if (rc_bwd != LORA_RC_OK) {
        fprintf(stderr, "  backward dispatch rc=%d\n", rc_bwd);
        return 1;
    }
    closed_form_backward(x, A, B, dy, dA_ref, dB_ref, dx_ref, M, N, K, R, alpha);
    float dA_max = max_abs_diff(dA_dispatch, dA_ref, K*R);
    float dB_max = max_abs_diff(dB_dispatch, dB_ref, R*N);
    float dx_max = max_abs_diff(dx_dispatch, dx_ref, M*K);

    const float TOL = 1e-3f;
    int pass = (dy_max < TOL) && (dA_max < TOL) && (dB_max < TOL) && (dx_max < TOL);
    printf("  M=%d N=%d K=%d R=%d alpha=%.3f apply_base=%d is_device=%d\n",
           M, N, K, R, alpha, apply_base, is_device);
    printf("    |Δy| =%.3e  |ΔdA|=%.3e  |ΔdB|=%.3e  |Δdx|=%.3e   %s\n",
           (double)dy_max, (double)dA_max, (double)dB_max, (double)dx_max,
           pass ? "PASS" : "FAIL");

    free(x); free(W); free(A); free(B);
    free(y_dispatch); free(y_ref); free(tmp); free(u);
    free(dy); free(dA_dispatch); free(dB_dispatch); free(dx_dispatch);
    free(dA_ref); free(dB_ref); free(dx_ref);
    return pass ? 0 : 1;
}

int main(int argc, char** argv) {
    int is_device = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--device") == 0) is_device = 1;
    }

    if (is_device && !lora_cuda_runtime_available()) {
        printf("[SKIP] --device requested but CUDA runtime not linked. "
               "Build with HXQWEN14B_CUDA=1 + nvcc to test the GPU path.\n");
        printf("       Acceptance F-CUDA-LORA-1 (vs PyTorch) is PENDING-HARDWARE.\n");
        return 0;
    }

    printf("[F-CUDA-LORA-1] LoRA fwd+bwd numerical equivalence test (%s path)\n",
           is_device ? "CUDA" : "CPU");
    printf("  tolerance: |Δ| < 1e-3 absolute (per-element max)\n");

    int fails = 0;
    // Spec case: r=64 default, mid-size M/N/K so the kernel actually tiles.
    fails += run_one(/*M=*/8,  /*N=*/128, /*K=*/128, /*R=*/64, 16.0f / 64.0f, /*apply_base=*/1, is_device);
    // Apply-base off (LoRA-delta only).
    fails += run_one(8,  128, 128, 64, 0.25f, 0, is_device);
    // Smaller rank exercise (r=8) — verifies the rank-loop is actually variable.
    fails += run_one(4,  64,  64,  8,  16.0f / 8.0f,  1, is_device);
    // Non-square — different M, N, K to catch any swapped-dim bug.
    fails += run_one(16, 32,  96,  16, 0.125f,        1, is_device);

    printf("\n[F-CUDA-LORA-1] %s — %d case(s) failed\n",
           fails == 0 ? "PASS" : "FAIL", fails);
    if (fails == 0 && !is_device) {
        printf("  Note: GPU-vs-CPU acceptance is PENDING-HARDWARE.\n");
        printf("        Re-run on RTX 5070 (sm_120) or H100 (sm_90) with --device\n");
        printf("        after building libhxqwen14b.so via HXQWEN14B_CUDA=1.\n");
    }
    return fails ? 1 : 0;
}
