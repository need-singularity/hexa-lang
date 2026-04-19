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

#endif /* __linux__ */

// ─────────────────────────────────────────────────────────────
// Platform-independent includes + functions (no BLAS dependency)
// ─────────────────────────────────────────────────────────────
#ifndef __linux__
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#endif

int64_t hxblas_version(void) {
    return 3;  // bumped 2026-04-20: + hxckpt_* binary ckpt FFI
}

// =============================================================
// HEXACKPT-v2 binary ckpt FFI (2026-04-20) - Linux port
// Mirrors anima/training/hxblas_wrapper.c (Mac Accelerate build).
// See that file for format spec + design rationale. All little-endian.
// =============================================================

#include <stdio.h>

int64_t hxckpt_open_append(int64_t path_p) {
    const char* path = (const char*)(uintptr_t)path_p;
    if (!path) return 0;
    FILE* f = fopen(path, "ab");
    return (int64_t)(uintptr_t)f;
}
int64_t hxckpt_open_write(int64_t path_p) {
    const char* path = (const char*)(uintptr_t)path_p;
    if (!path) return 0;
    FILE* f = fopen(path, "wb");
    return (int64_t)(uintptr_t)f;
}
int64_t hxckpt_open_read(int64_t path_p) {
    const char* path = (const char*)(uintptr_t)path_p;
    if (!path) return 0;
    FILE* f = fopen(path, "rb");
    return (int64_t)(uintptr_t)f;
}
int64_t hxckpt_close(int64_t handle) {
    FILE* f = (FILE*)(uintptr_t)handle;
    if (!f) return -1;
    return (int64_t)fclose(f);
}
int64_t hxckpt_write_bytes(int64_t handle, int64_t buf_p, int64_t n_bytes) {
    FILE* f = (FILE*)(uintptr_t)handle;
    const void* buf = (const void*)(uintptr_t)buf_p;
    if (!f || !buf || n_bytes < 0) return -1;
    size_t got = fwrite(buf, 1, (size_t)n_bytes, f);
    return (got == (size_t)n_bytes) ? 0 : -2;
}
int64_t hxckpt_read_bytes(int64_t handle, int64_t buf_p, int64_t n_bytes) {
    FILE* f = (FILE*)(uintptr_t)handle;
    void* buf = (void*)(uintptr_t)buf_p;
    if (!f || !buf || n_bytes < 0) return -1;
    size_t got = fread(buf, 1, (size_t)n_bytes, f);
    return (int64_t)got;
}
int64_t hxckpt_write_u32(int64_t handle, int64_t val) {
    FILE* f = (FILE*)(uintptr_t)handle;
    if (!f) return -1;
    uint32_t v = (uint32_t)val;
    unsigned char b[4];
    b[0] = (unsigned char)(v & 0xFF);
    b[1] = (unsigned char)((v >> 8) & 0xFF);
    b[2] = (unsigned char)((v >> 16) & 0xFF);
    b[3] = (unsigned char)((v >> 24) & 0xFF);
    return (fwrite(b, 1, 4, f) == 4) ? 0 : -2;
}
int64_t hxckpt_read_u32(int64_t handle) {
    FILE* f = (FILE*)(uintptr_t)handle;
    if (!f) return -1;
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) return -1;
    uint32_t v = (uint32_t)b[0] | ((uint32_t)b[1] << 8)
               | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return (int64_t)v;
}
int64_t hxckpt_write_u64(int64_t handle, int64_t val) {
    FILE* f = (FILE*)(uintptr_t)handle;
    if (!f) return -1;
    uint64_t v = (uint64_t)val;
    unsigned char b[8];
    for (int i = 0; i < 8; i++) b[i] = (unsigned char)((v >> (i * 8)) & 0xFF);
    return (fwrite(b, 1, 8, f) == 8) ? 0 : -2;
}
int64_t hxckpt_read_u64(int64_t handle) {
    FILE* f = (FILE*)(uintptr_t)handle;
    if (!f) return -1;
    unsigned char b[8];
    if (fread(b, 1, 8, f) != 8) return -1;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)b[i]) << (i * 8);
    return (int64_t)v;
}
int64_t hxckpt_write_u16(int64_t handle, int64_t val) {
    FILE* f = (FILE*)(uintptr_t)handle;
    if (!f) return -1;
    uint16_t v = (uint16_t)val;
    unsigned char b[2];
    b[0] = (unsigned char)(v & 0xFF);
    b[1] = (unsigned char)((v >> 8) & 0xFF);
    return (fwrite(b, 1, 2, f) == 2) ? 0 : -2;
}
int64_t hxckpt_read_u16(int64_t handle) {
    FILE* f = (FILE*)(uintptr_t)handle;
    if (!f) return -1;
    unsigned char b[2];
    if (fread(b, 1, 2, f) != 2) return -1;
    uint16_t v = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return (int64_t)v;
}
int64_t hxckpt_write_u8(int64_t handle, int64_t val) {
    FILE* f = (FILE*)(uintptr_t)handle;
    if (!f) return -1;
    unsigned char b = (unsigned char)(val & 0xFF);
    return (fwrite(&b, 1, 1, f) == 1) ? 0 : -2;
}
int64_t hxckpt_read_u8(int64_t handle) {
    FILE* f = (FILE*)(uintptr_t)handle;
    if (!f) return -1;
    unsigned char b;
    if (fread(&b, 1, 1, f) != 1) return -1;
    return (int64_t)b;
}
int64_t hxckpt_write_str_bytes(int64_t handle, int64_t str_p, int64_t slen) {
    FILE* f = (FILE*)(uintptr_t)handle;
    const char* s = (const char*)(uintptr_t)str_p;
    if (!f || !s || slen < 0) return -1;
    return (fwrite(s, 1, (size_t)slen, f) == (size_t)slen) ? 0 : -2;
}
int64_t hxckpt_file_size(int64_t path_p) {
    const char* path = (const char*)(uintptr_t)path_p;
    if (!path) return -1;
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    fclose(f);
    return (int64_t)sz;
}

#ifndef __linux__
// Mac/other: scalar reference kernel for benchmarking
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
#endif

// ═════════════════════════════════════════════════════════════════════
// BF16 NEON KERNELS — ARM64 native bf16 matmul/matvec with f32 accum
//
// bf16 format: 1 sign + 8 exponent + 7 mantissa (upper 16 bits of f32)
// Memory bandwidth halved vs f32 → theoretical 2x throughput for
// bandwidth-bound ops (inference matvec, weight loading).
//
// On ARM64 (Apple M4 / ARMv9): uses NEON 128-bit SIMD.
//   8 × bf16 per register, accumulate in f32 (4 per register).
//   vfmaq_f32 = fused multiply-add, no intermediate rounding.
//
// On non-ARM: scalar fallback (same semantics, no SIMD).
//
// Build (macOS ARM64):
//   clang -O3 -fPIC -dynamiclib -arch arm64 hxblas_linux.c \
//         -o build/libhxblas.dylib -lm
//
// Build (Linux aarch64):
//   gcc -O3 -fPIC -shared -march=armv8.2-a+bf16 hxblas_linux.c \
//       -o build/libhxblas.so -lm
// ═════════════════════════════════════════════════════════════════════

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __aarch64__
#include <arm_neon.h>
#endif

// ── bf16 ↔ f32 scalar conversion ─────────────────────────────────

static inline float bf16_to_f32(uint16_t x) {
    // bf16 is the upper 16 bits of IEEE 754 f32.
    // Shift left 16 to reconstruct the f32 bit pattern.
    uint32_t bits = (uint32_t)x << 16;
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

static inline uint16_t f32_to_bf16(float x) {
    // Truncate: take upper 16 bits of f32.
    // Round-to-nearest-even: add rounding bias before truncation.
    uint32_t bits;
    memcpy(&bits, &x, 4);
    // Round to nearest even: add 0x7FFF + lsb(bit16) for tie-break
    uint32_t lsb = (bits >> 16) & 1;
    bits += 0x7FFFu + lsb;
    return (uint16_t)(bits >> 16);
}

// ── bf16 pack/unpack (f32 ↔ bf16 bulk conversion) ────────────────

void hxblas_f32_to_bf16(int64_t n, int64_t src_f32, int64_t dst_bf16) {
    const float* src = (const float*)(uintptr_t)src_f32;
    uint16_t* dst = (uint16_t*)(uintptr_t)dst_bf16;
#ifdef __aarch64__
    int64_t i = 0;
    // Process 4 floats at a time using NEON
    for (; i + 3 < n; i += 4) {
        float32x4_t v = vld1q_f32(src + i);
        // Extract upper 16 bits: reinterpret as u32, shift right 16
        // But we want round-to-nearest, so do scalar for correctness
        dst[i + 0] = f32_to_bf16(src[i + 0]);
        dst[i + 1] = f32_to_bf16(src[i + 1]);
        dst[i + 2] = f32_to_bf16(src[i + 2]);
        dst[i + 3] = f32_to_bf16(src[i + 3]);
    }
    for (; i < n; i++) {
        dst[i] = f32_to_bf16(src[i]);
    }
#else
    for (int64_t i = 0; i < n; i++) {
        dst[i] = f32_to_bf16(src[i]);
    }
#endif
}

void hxblas_bf16_to_f32(int64_t n, int64_t src_bf16, int64_t dst_f32) {
    const uint16_t* src = (const uint16_t*)(uintptr_t)src_bf16;
    float* dst = (float*)(uintptr_t)dst_f32;
#ifdef __aarch64__
    int64_t i = 0;
    // Process 8 bf16 values at a time → 2 × float32x4_t
    for (; i + 7 < n; i += 8) {
        uint16x8_t bv = vld1q_u16(src + i);
        // Low 4: extend to u32 and shift left 16
        uint16x4_t lo4 = vget_low_u16(bv);
        uint16x4_t hi4 = vget_high_u16(bv);
        uint32x4_t lo32 = vshll_n_u16(lo4, 16);
        uint32x4_t hi32 = vshll_n_u16(hi4, 16);
        float32x4_t flo = vreinterpretq_f32_u32(lo32);
        float32x4_t fhi = vreinterpretq_f32_u32(hi32);
        vst1q_f32(dst + i, flo);
        vst1q_f32(dst + i + 4, fhi);
    }
    for (; i < n; i++) {
        dst[i] = bf16_to_f32(src[i]);
    }
#else
    for (int64_t i = 0; i < n; i++) {
        dst[i] = bf16_to_f32(src[i]);
    }
#endif
}

// ── NEON bf16 GEMM: A[M×K] bf16 × B[K×N] bf16 → C[M×N] f32 ─────
//
// Micro-kernel strategy:
//   - 4×4 tile: accumulate 4 rows × 4 cols in 4 f32x4 accumulators
//   - Inner loop: load 8 bf16 from A row, 8 bf16 from B col,
//     widen to f32, FMA
//   - K dimension processed in chunks of 8 (NEON register width for bf16)
//
// For M4 (ARMv9): 32 NEON registers, 4×4 tile uses 4 accum + 2 load = 6 regs

void hxblas_bgemm(int64_t M, int64_t K, int64_t N,
                  int64_t A_ptr, int64_t B_ptr, int64_t C_ptr) {
    const uint16_t* A = (const uint16_t*)(uintptr_t)A_ptr;
    const uint16_t* B = (const uint16_t*)(uintptr_t)B_ptr;
    float* C = (float*)(uintptr_t)C_ptr;

#ifdef __aarch64__
    // Zero output
    memset(C, 0, (size_t)(M * N) * sizeof(float));

    // 4×4 tiled micro-kernel with f32 accumulation
    int64_t mi, ni, ki;

    for (mi = 0; mi + 3 < M; mi += 4) {
        for (ni = 0; ni + 3 < N; ni += 4) {
            // 4×4 f32 accumulators (4 rows × 4 cols)
            float32x4_t c00 = vdupq_n_f32(0.0f);
            float32x4_t c10 = vdupq_n_f32(0.0f);
            float32x4_t c20 = vdupq_n_f32(0.0f);
            float32x4_t c30 = vdupq_n_f32(0.0f);

            // Process K in chunks of 4 (half a NEON u16x8 register)
            for (ki = 0; ki + 3 < K; ki += 4) {
                // Load 4 bf16 values from each of 4 A rows → widen to f32
                // A[row][ki..ki+3]
                uint16x4_t a0_16 = vld1_u16(&A[(mi + 0) * K + ki]);
                uint16x4_t a1_16 = vld1_u16(&A[(mi + 1) * K + ki]);
                uint16x4_t a2_16 = vld1_u16(&A[(mi + 2) * K + ki]);
                uint16x4_t a3_16 = vld1_u16(&A[(mi + 3) * K + ki]);

                // Widen bf16 → f32: shift left 16 bits
                float32x4_t a0 = vreinterpretq_f32_u32(vshll_n_u16(a0_16, 16));
                float32x4_t a1 = vreinterpretq_f32_u32(vshll_n_u16(a1_16, 16));
                float32x4_t a2 = vreinterpretq_f32_u32(vshll_n_u16(a2_16, 16));
                float32x4_t a3 = vreinterpretq_f32_u32(vshll_n_u16(a3_16, 16));

                // Load 4 bf16 from each of 4 B rows, FMA with constant lane index
                // B is row-major: B[ki+p][ni..ni+3]
                // Lane index MUST be compile-time constant — manually unrolled
                {
                    uint16x4_t b0_16 = vld1_u16(&B[(ki + 0) * N + ni]);
                    float32x4_t bv0 = vreinterpretq_f32_u32(vshll_n_u16(b0_16, 16));
                    c00 = vfmaq_laneq_f32(c00, bv0, a0, 0);
                    c10 = vfmaq_laneq_f32(c10, bv0, a1, 0);
                    c20 = vfmaq_laneq_f32(c20, bv0, a2, 0);
                    c30 = vfmaq_laneq_f32(c30, bv0, a3, 0);

                    uint16x4_t b1_16 = vld1_u16(&B[(ki + 1) * N + ni]);
                    float32x4_t bv1 = vreinterpretq_f32_u32(vshll_n_u16(b1_16, 16));
                    c00 = vfmaq_laneq_f32(c00, bv1, a0, 1);
                    c10 = vfmaq_laneq_f32(c10, bv1, a1, 1);
                    c20 = vfmaq_laneq_f32(c20, bv1, a2, 1);
                    c30 = vfmaq_laneq_f32(c30, bv1, a3, 1);

                    uint16x4_t b2_16 = vld1_u16(&B[(ki + 2) * N + ni]);
                    float32x4_t bv2 = vreinterpretq_f32_u32(vshll_n_u16(b2_16, 16));
                    c00 = vfmaq_laneq_f32(c00, bv2, a0, 2);
                    c10 = vfmaq_laneq_f32(c10, bv2, a1, 2);
                    c20 = vfmaq_laneq_f32(c20, bv2, a2, 2);
                    c30 = vfmaq_laneq_f32(c30, bv2, a3, 2);

                    uint16x4_t b3_16 = vld1_u16(&B[(ki + 3) * N + ni]);
                    float32x4_t bv3 = vreinterpretq_f32_u32(vshll_n_u16(b3_16, 16));
                    c00 = vfmaq_laneq_f32(c00, bv3, a0, 3);
                    c10 = vfmaq_laneq_f32(c10, bv3, a1, 3);
                    c20 = vfmaq_laneq_f32(c20, bv3, a2, 3);
                    c30 = vfmaq_laneq_f32(c30, bv3, a3, 3);
                }
            }

            // Store 4×4 tile to C
            vst1q_f32(&C[(mi + 0) * N + ni], vaddq_f32(vld1q_f32(&C[(mi + 0) * N + ni]), c00));
            vst1q_f32(&C[(mi + 1) * N + ni], vaddq_f32(vld1q_f32(&C[(mi + 1) * N + ni]), c10));
            vst1q_f32(&C[(mi + 2) * N + ni], vaddq_f32(vld1q_f32(&C[(mi + 2) * N + ni]), c20));
            vst1q_f32(&C[(mi + 3) * N + ni], vaddq_f32(vld1q_f32(&C[(mi + 3) * N + ni]), c30));

            // Handle remaining K elements (scalar)
            for (int64_t kk = ki; kk < K; kk++) {
                float b0 = bf16_to_f32(B[kk * N + ni + 0]);
                float b1 = bf16_to_f32(B[kk * N + ni + 1]);
                float b2 = bf16_to_f32(B[kk * N + ni + 2]);
                float b3 = bf16_to_f32(B[kk * N + ni + 3]);
                for (int r = 0; r < 4; r++) {
                    float av = bf16_to_f32(A[(mi + r) * K + kk]);
                    C[(mi + r) * N + ni + 0] += av * b0;
                    C[(mi + r) * N + ni + 1] += av * b1;
                    C[(mi + r) * N + ni + 2] += av * b2;
                    C[(mi + r) * N + ni + 3] += av * b3;
                }
            }
        }

        // Handle remaining N columns (scalar)
        for (int64_t nj = ni; nj < N; nj++) {
            for (int r = 0; r < 4; r++) {
                float sum = 0.0f;
                for (int64_t kk = 0; kk < K; kk++) {
                    sum += bf16_to_f32(A[(mi + r) * K + kk]) * bf16_to_f32(B[kk * N + nj]);
                }
                C[(mi + r) * N + nj] = sum;
            }
        }
    }

    // Handle remaining M rows (scalar)
    for (int64_t mj = mi; mj < M; mj++) {
        for (int64_t nj = 0; nj < N; nj++) {
            float sum = 0.0f;
            for (int64_t kk = 0; kk < K; kk++) {
                sum += bf16_to_f32(A[mj * K + kk]) * bf16_to_f32(B[kk * N + nj]);
            }
            C[mj * N + nj] = sum;
        }
    }

#else
    // Scalar fallback for non-ARM targets
    for (int64_t i = 0; i < M; i++) {
        for (int64_t j = 0; j < N; j++) {
            float sum = 0.0f;
            for (int64_t k = 0; k < K; k++) {
                sum += bf16_to_f32(A[i * K + k]) * bf16_to_f32(B[k * N + j]);
            }
            C[i * N + j] = sum;
        }
    }
#endif
}

// ── NEON bf16 GEMV: A[M×K] bf16 × x[K] f32 → y[M] f32 ──────────
//
// Critical path for inference: weight (bf16) × activation (f32).
// Weights stay in bf16 (half memory), activations in f32 (full precision).
// Inner loop: widen bf16 weight to f32, FMA with f32 activation.

void hxblas_bgemv(int64_t M, int64_t K,
                  int64_t A_ptr, int64_t x_ptr, int64_t y_ptr) {
    const uint16_t* A = (const uint16_t*)(uintptr_t)A_ptr;
    const float* x = (const float*)(uintptr_t)x_ptr;
    float* y = (float*)(uintptr_t)y_ptr;

#ifdef __aarch64__
    for (int64_t i = 0; i < M; i++) {
        const uint16_t* row = A + i * K;
        float32x4_t acc0 = vdupq_n_f32(0.0f);
        float32x4_t acc1 = vdupq_n_f32(0.0f);

        int64_t k = 0;
        // Process 8 elements per iteration (two f32x4 accumulators)
        for (; k + 7 < K; k += 8) {
            // Load 8 bf16 weights
            uint16x8_t w16 = vld1q_u16(row + k);
            // Widen lower 4 and upper 4 to f32
            uint16x4_t wlo = vget_low_u16(w16);
            uint16x4_t whi = vget_high_u16(w16);
            float32x4_t w0 = vreinterpretq_f32_u32(vshll_n_u16(wlo, 16));
            float32x4_t w1 = vreinterpretq_f32_u32(vshll_n_u16(whi, 16));

            // Load 8 f32 activations
            float32x4_t x0 = vld1q_f32(x + k);
            float32x4_t x1 = vld1q_f32(x + k + 4);

            // FMA
            acc0 = vfmaq_f32(acc0, w0, x0);
            acc1 = vfmaq_f32(acc1, w1, x1);
        }

        // Process remaining 4 elements
        for (; k + 3 < K; k += 4) {
            uint16x4_t w16 = vld1_u16(row + k);
            float32x4_t w0 = vreinterpretq_f32_u32(vshll_n_u16(w16, 16));
            float32x4_t x0 = vld1q_f32(x + k);
            acc0 = vfmaq_f32(acc0, w0, x0);
        }

        // Horizontal sum
        float32x4_t sum4 = vaddq_f32(acc0, acc1);
        float sum = vaddvq_f32(sum4);

        // Scalar tail
        for (; k < K; k++) {
            sum += bf16_to_f32(row[k]) * x[k];
        }

        y[i] = sum;
    }

#else
    // Scalar fallback
    for (int64_t i = 0; i < M; i++) {
        float sum = 0.0f;
        for (int64_t k = 0; k < K; k++) {
            sum += bf16_to_f32(A[i * K + k]) * x[k];
        }
        y[i] = sum;
    }
#endif
}

// ── bf16 ABI version ─────────────────────────────────────────────

int64_t hxblas_bf16_version(void) {
    // Version 1: initial bf16 NEON kernels (bgemm, bgemv, pack/unpack)
    return 1;
}
