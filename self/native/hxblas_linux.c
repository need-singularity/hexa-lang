// hxblas_linux.c — hexa FFI ↔ OpenBLAS/libm ABI shim (Linux x86_64)
//
// Port of /Users/ghost/Dev/anima/training/hxblas_wrapper.c from macOS
// Accelerate → Linux OpenBLAS (cblas.h). ABI matches bit-for-bit so the
// same hexa-side extern declarations (same function names, same
// int64/double/pointer-as-int64 signature) resolve to this .so on Linux
// and the Mac .dylib on Darwin.
//
// Build (Linux x86_64, Ubuntu/Debian with libopenblas-dev):
//   gcc -O3 -fPIC -shared -fopenmp self/native/hxblas_linux.c \
//       -o self/native/build/libhxblas.so -lopenblas -lgomp -lm
//
//   See scripts/build_hxblas_linux.hexa for the canonical builder.
//
// Deployment on H100 pod:
//   cp self/native/build/libhxblas.so /usr/local/lib/libhxblas.so
//   ldconfig                          # OR set LD_LIBRARY_PATH
//   # hexa source points to @link("libhxblas") → libhxblas.so resolved
//
// Linkage strategy (single file, no platform #ifdef):
//   * Linux OpenBLAS ships <cblas.h> at /usr/include/x86_64-linux-gnu/cblas.h
//   * The enum CBLAS_ORDER / CBLAS_TRANSPOSE values are the same magic
//     numbers (101/102, 111/112/113) on every cblas implementation — the
//     hexa call `hxblas_sgemm(101, 111, 111, ...)` is portable.
//
// Expected Linux speedup vs Mac baseline (2026-04-16):
//   * Mac M3, Accelerate cblas_sgemm 128² ≈ 0.08 ms (15x vs scalar C)
//   * Linux x86_64 + AVX-512 + OpenBLAS is typically parity or better at
//     large sizes (256²+) because of wider SIMD and dedicated cblas
//     threading. Target 15-30% acceleration on workloads that were
//     previously scalar. The key is that the Linux version exists — the
//     ML roadmap was Mac-only until this commit.
//
// .hexanoport marker convention:
//   See hexa_cc.c.hexanoport — this .c is permitted because it is a
//   native shim (extern FFI boundary), not a compilation target.

#ifdef __linux__

#include <cblas.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

// ─────────────────────────────────────────────────────────────
// cblas_sgemm shim — C = alpha * A · B + beta * C
// Matches the exact Mac hxblas signature so hexa side is unchanged.
// ─────────────────────────────────────────────────────────────
void hxblas_sgemm(int64_t order, int64_t transA, int64_t transB,
                  int64_t M, int64_t N, int64_t K,
                  double alpha,
                  int64_t A, int64_t lda,
                  int64_t B, int64_t ldb,
                  double beta,
                  int64_t C, int64_t ldc) {
    cblas_sgemm((enum CBLAS_ORDER)order,
                (enum CBLAS_TRANSPOSE)transA,
                (enum CBLAS_TRANSPOSE)transB,
                (int)M, (int)N, (int)K,
                (float)alpha,
                (const float*)(uintptr_t)A, (int)lda,
                (const float*)(uintptr_t)B, (int)ldb,
                (float)beta,
                (float*)(uintptr_t)C, (int)ldc);
}

double hxblas_sdot(int64_t N, int64_t x, int64_t incx, int64_t y, int64_t incy) {
    float r = cblas_sdot((int)N,
                         (const float*)(uintptr_t)x, (int)incx,
                         (const float*)(uintptr_t)y, (int)incy);
    return (double)r;
}

void hxblas_saxpy(int64_t N, double alpha,
                  int64_t x, int64_t incx,
                  int64_t y, int64_t incy) {
    cblas_saxpy((int)N, (float)alpha,
                (const float*)(uintptr_t)x, (int)incx,
                (float*)(uintptr_t)y, (int)incy);
}

void hxblas_sscal(int64_t N, double alpha, int64_t x, int64_t incx) {
    cblas_sscal((int)N, (float)alpha, (float*)(uintptr_t)x, (int)incx);
}

// ─────────────────────────────────────────────────────────────
// Reference kernels for benchmark triangulation (identical to Mac)
// ─────────────────────────────────────────────────────────────
void hxblas_matmul_scalar(int64_t M, int64_t K, int64_t N,
                          int64_t A, int64_t B, int64_t C) {
    const float* a = (const float*)(uintptr_t)A;
    const float* b = (const float*)(uintptr_t)B;
    float* c = (float*)(uintptr_t)C;
    for (int64_t i = 0; i < M; i++) {
        for (int64_t j = 0; j < N; j++) {
            float sum = 0.0f;
            for (int64_t k = 0; k < K; k++) {
                sum += a[i * K + k] * b[k * N + j];
            }
            c[i * N + j] = sum;
        }
    }
}

void hxblas_matmul_omp(int64_t M, int64_t K, int64_t N,
                       int64_t A, int64_t B, int64_t C) {
    // v1: for small M, OMP thread spawn overhead dominates (224-thread H100
    // host measured 63ms vs 2.5ms scalar at 128²). Route small cases to
    // scalar or cap thread count.
    if (M * N * K <= 512LL * 512 * 512) {
        // Below ~512³ FLOPs, scalar is faster than OMP on many-core hosts.
        hxblas_matmul_scalar(M, K, N, A, B, C);
        return;
    }
    const float* a = (const float*)(uintptr_t)A;
    const float* b = (const float*)(uintptr_t)B;
    float* c = (float*)(uintptr_t)C;
#ifdef _OPENMP
    #pragma omp parallel for schedule(static) num_threads(8)
#endif
    for (int64_t i = 0; i < M; i++) {
        for (int64_t j = 0; j < N; j++) {
            float sum = 0.0f;
            for (int64_t k = 0; k < K; k++) {
                sum += a[i * K + k] * b[k * N + j];
            }
            c[i * N + j] = sum;
        }
    }
}

// ═════════════════════════════════════════════════════════════
// EXTENSION — whole-tensor eltwise + attention primitives.
// Port of the "attention + cross-entropy + embed" block from the Mac
// wrapper. On Linux these rely on plain scalar loops — gcc -O3 on
// x86_64 auto-vectorises them into AVX2/AVX-512 when the CPU supports
// it. No vDSP dependency.
// ═════════════════════════════════════════════════════════════

void hxblas_axpy_inplace(int64_t N, int64_t dst, int64_t src) {
    float* d = (float*)(uintptr_t)dst;
    const float* s = (const float*)(uintptr_t)src;
    for (int64_t i = 0; i < N; i++) d[i] += s[i];
}

void hxblas_copy(int64_t N, int64_t dst, int64_t src) {
    memcpy((void*)(uintptr_t)dst, (const void*)(uintptr_t)src, (size_t)N * sizeof(float));
}

void hxblas_zero(int64_t N, int64_t dst) {
    memset((void*)(uintptr_t)dst, 0, (size_t)N * sizeof(float));
}

void hxblas_sgd_inplace(int64_t N, double lr, int64_t w, int64_t g) {
    float* W = (float*)(uintptr_t)w;
    const float* G = (const float*)(uintptr_t)g;
    const float lrf = (float)lr;
    for (int64_t i = 0; i < N; i++) W[i] -= lrf * G[i];
}

void hxblas_silu_fwd(int64_t N, int64_t out, int64_t x) {
    float* o = (float*)(uintptr_t)out;
    const float* X = (const float*)(uintptr_t)x;
    for (int64_t i = 0; i < N; i++) {
        float v = X[i];
        float s = 1.0f / (1.0f + expf(-v));
        o[i] = v * s;
    }
}

void hxblas_silu_bwd(int64_t N, int64_t dx, int64_t x, int64_t dy) {
    float* DX = (float*)(uintptr_t)dx;
    const float* X = (const float*)(uintptr_t)x;
    const float* DY = (const float*)(uintptr_t)dy;
    for (int64_t i = 0; i < N; i++) {
        float v = X[i];
        float s = 1.0f / (1.0f + expf(-v));
        DX[i] = DY[i] * (s + v * s * (1.0f - s));
    }
}

void hxblas_rmsnorm_fwd(int64_t S, int64_t D, double eps,
                        int64_t y_p, int64_t x_p, int64_t g_p) {
    float* y = (float*)(uintptr_t)y_p;
    const float* x = (const float*)(uintptr_t)x_p;
    const float* g = (const float*)(uintptr_t)g_p;
    const float epsf = (float)eps;
    const float invD = 1.0f / (float)D;
    for (int64_t i = 0; i < S; i++) {
        const float* xi = x + i * D;
        float* yi = y + i * D;
        float ms = 0.0f;
        for (int64_t p = 0; p < D; p++) ms += xi[p] * xi[p];
        ms *= invD;
        float inv_r = 1.0f / sqrtf(ms + epsf);
        for (int64_t p = 0; p < D; p++) yi[p] = xi[p] * inv_r * g[p];
    }
}

void hxblas_rmsnorm_bwd(int64_t S, int64_t D, double eps,
                        int64_t dx_p, int64_t dg_p,
                        int64_t x_p, int64_t g_p, int64_t dy_p) {
    float* dx = (float*)(uintptr_t)dx_p;
    float* dg = (float*)(uintptr_t)dg_p;
    const float* x = (const float*)(uintptr_t)x_p;
    const float* g = (const float*)(uintptr_t)g_p;
    const float* dy = (const float*)(uintptr_t)dy_p;
    const float epsf = (float)eps;
    const float invD = 1.0f / (float)D;
    for (int64_t i = 0; i < S; i++) {
        const float* xi = x + i * D;
        const float* dyi = dy + i * D;
        float* dxi = dx + i * D;
        float ms = 0.0f;
        for (int64_t p = 0; p < D; p++) ms += xi[p] * xi[p];
        ms *= invD;
        float r = sqrtf(ms + epsf);
        float inv_r = 1.0f / r;
        float inv_r3 = inv_r * inv_r * inv_r;
        float coef = 0.0f;
        for (int64_t p = 0; p < D; p++) coef += xi[p] * g[p] * dyi[p];
        for (int64_t p = 0; p < D; p++) {
            dxi[p] = g[p] * dyi[p] * inv_r - xi[p] * coef * inv_r3 * invD;
            dg[p] += xi[p] * dyi[p] * inv_r;
        }
    }
}

void hxblas_attn_softmax_causal(int64_t S, double scale_d, int64_t scores_p) {
    float* sc = (float*)(uintptr_t)scores_p;
    const float scale = (float)scale_d;
    const float NEG = -1e30f;
    for (int64_t i = 0; i < S; i++) {
        float* row = sc + i * S;
        for (int64_t j = 0; j <= i; j++) row[j] = row[j] * scale;
        for (int64_t j = i + 1; j < S; j++) row[j] = NEG;
        float mx = row[0];
        for (int64_t j = 1; j <= i; j++) if (row[j] > mx) mx = row[j];
        float sum = 0.0f;
        for (int64_t j = 0; j <= i; j++) {
            float e = expf(row[j] - mx);
            row[j] = e;
            sum += e;
        }
        for (int64_t j = i + 1; j < S; j++) row[j] = 0.0f;
        float inv_sum = 1.0f / sum;
        for (int64_t j = 0; j <= i; j++) row[j] *= inv_sum;
    }
}

void hxblas_attn_softmax_bwd(int64_t S, double scale_d,
                              int64_t dscores_p, int64_t probs_p, int64_t dprobs_p) {
    float* ds = (float*)(uintptr_t)dscores_p;
    const float* p = (const float*)(uintptr_t)probs_p;
    const float* dp = (const float*)(uintptr_t)dprobs_p;
    const float scale = (float)scale_d;
    for (int64_t i = 0; i < S; i++) {
        const float* prow = p + i * S;
        const float* dprow = dp + i * S;
        float* dsrow = ds + i * S;
        float s_acc = 0.0f;
        for (int64_t j = 0; j <= i; j++) s_acc += prow[j] * dprow[j];
        for (int64_t j = 0; j <= i; j++) dsrow[j] = prow[j] * (dprow[j] - s_acc) * scale;
        for (int64_t j = i + 1; j < S; j++) dsrow[j] = 0.0f;
    }
}

double hxblas_cross_entropy(int64_t S, int64_t V, double scale_d,
                            int64_t logits_p, int64_t dlogits_p, int64_t targets_p) {
    const float* logits = (const float*)(uintptr_t)logits_p;
    float* dlogits = (float*)(uintptr_t)dlogits_p;
    const int64_t* targets = (const int64_t*)(uintptr_t)targets_p;
    const float scale = (float)scale_d;
    double loss_acc = 0.0;
    for (int64_t i = 0; i < S; i++) {
        const float* row = logits + i * V;
        float* drow = dlogits + i * V;
        float mx = row[0];
        for (int64_t j = 1; j < V; j++) if (row[j] > mx) mx = row[j];
        float sum_e = 0.0f;
        for (int64_t j = 0; j < V; j++) sum_e += expf(row[j] - mx);
        float log_sum = mx + logf(sum_e);
        int64_t t = targets[i];
        loss_acc += (double)(log_sum - row[t]);
        for (int64_t j = 0; j < V; j++) drow[j] = expf(row[j] - log_sum) * scale;
        drow[t] -= scale;
    }
    return loss_acc;
}

void hxblas_embed_lookup(int64_t S, int64_t D, int64_t h_p, int64_t embed_p, int64_t tokens_p) {
    float* h = (float*)(uintptr_t)h_p;
    const float* embed = (const float*)(uintptr_t)embed_p;
    const int64_t* tokens = (const int64_t*)(uintptr_t)tokens_p;
    for (int64_t i = 0; i < S; i++) {
        int64_t t = tokens[i];
        memcpy(h + i * D, embed + t * D, (size_t)D * sizeof(float));
    }
}

void hxblas_embed_scatter(int64_t S, int64_t D, int64_t dembed_p, int64_t dh_p, int64_t tokens_p) {
    float* dembed = (float*)(uintptr_t)dembed_p;
    const float* dh = (const float*)(uintptr_t)dh_p;
    const int64_t* tokens = (const int64_t*)(uintptr_t)tokens_p;
    for (int64_t i = 0; i < S; i++) {
        int64_t t = tokens[i];
        float* row = dembed + t * D;
        const float* src = dh + i * D;
        for (int64_t p = 0; p < D; p++) row[p] += src[p];
    }
}

int64_t hxblas_version(void) {
    // Matches the Mac wrapper ABI version so hexa callers treat them as one.
    return 2;
}

#endif /* __linux__ */
