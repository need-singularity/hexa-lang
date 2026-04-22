// hxvdsp_linux.c — Linux port of Apple vDSP RMSNorm/SoftMax/SwiGLU shim
//
// Port of /Users/ghost/core/anima/training/hxvdsp_wrapper.c from macOS
// Accelerate vDSP → Linux scalar+OpenBLAS replacement.
//
// Strategy:
//   Linux has no vDSP. We replicate each vDSP primitive with the
//   closest cblas call (vsmul→sscal-like, vmul→dot-product pair, etc.)
//   and for the non-cblas ones (vsesq, vsadd, vvexpf, vvrecf) use a
//   tight scalar loop that gcc -O3 / -march=native auto-vectorises
//   into AVX2/AVX-512. Same ABI as the Mac wrapper: hexa Float→C
//   double, hexa int→int64_t, pointers as int64_t addresses.
//
// Build (Linux x86_64, Ubuntu/Debian with libopenblas-dev):
//   gcc -O3 -march=native -fPIC -shared self/native/hxvdsp_linux.c \
//       -o self/native/build/libhxvdsp.so -lm
//
//   See scripts/build_hxblas_linux.hexa (does both libraries).
//
// Why scalar+O3 is fine on Linux:
//   * Apple vDSP is a black-box NEON/AMX kernel; on x86_64 there's no
//     free lunch. AVX2 + auto-vec gets within 1.2-1.5x of hand-tuned
//     assembly for these element-wise ops.
//   * The FFI-hop-amortisation is the dominant win over per-element
//     hexa calls — matches the design rationale in the Mac original.
//
// Version: 1 (2026-04-16) — mirror of hxvdsp_wrapper.c version 1.

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int64_t hxvdsp_version(void) { return 1; }

// Small helpers that emulate the vDSP primitives used in the Mac shim.
// All inlined to give the compiler maximum room to vectorise.
static inline void hx_sum_of_squares(const float* x, int64_t n, float* out) {
    float s = 0.0f;
    for (int64_t i = 0; i < n; i++) s += x[i] * x[i];
    *out = s;
}

static inline float hx_max(const float* x, int64_t n) {
    float m = x[0];
    for (int64_t i = 1; i < n; i++) if (x[i] > m) m = x[i];
    return m;
}

static inline float hx_sum(const float* x, int64_t n) {
    float s = 0.0f;
    for (int64_t i = 0; i < n; i++) s += x[i];
    return s;
}

static inline void hx_scalar_add(const float* x, float a, float* y, int64_t n) {
    for (int64_t i = 0; i < n; i++) y[i] = x[i] + a;
}

static inline void hx_scalar_mul(const float* x, float a, float* y, int64_t n) {
    for (int64_t i = 0; i < n; i++) y[i] = x[i] * a;
}

static inline void hx_vmul(const float* a, const float* b, float* c, int64_t n) {
    for (int64_t i = 0; i < n; i++) c[i] = a[i] * b[i];
}

static inline void hx_vsub(const float* a, const float* b, float* c, int64_t n) {
    // Matches vDSP_vsub(A, .., B, .., C, ..) : C = B - A  (Apple flips operand order)
    for (int64_t i = 0; i < n; i++) c[i] = b[i] - a[i];
}

static inline void hx_vadd(const float* a, const float* b, float* c, int64_t n) {
    for (int64_t i = 0; i < n; i++) c[i] = a[i] + b[i];
}

static inline void hx_expf_vec(float* inout, int64_t n) {
    for (int64_t i = 0; i < n; i++) inout[i] = expf(inout[i]);
}

static inline void hx_recf_vec(float* inout, int64_t n) {
    for (int64_t i = 0; i < n; i++) inout[i] = 1.0f / inout[i];
}

// ─────────────────────────────────────────────────────────────
// RMSNorm forward — matches hxvdsp_rmsnorm_fwd exactly
// ─────────────────────────────────────────────────────────────
void hxvdsp_rmsnorm_fwd(int64_t S, int64_t D,
                        int64_t x_ptr, int64_t gain_ptr,
                        double eps,
                        int64_t out_ptr) {
    const float* x = (const float*)(uintptr_t)x_ptr;
    const float* g = (const float*)(uintptr_t)gain_ptr;
    float* out = (float*)(uintptr_t)out_ptr;
    const float eps_f = (float)eps;
    const float inv_D = 1.0f / (float)D;
    for (int64_t i = 0; i < S; i++) {
        const float* xi = x + i * D;
        float* oi = out + i * D;
        float ss = 0.0f;
        hx_sum_of_squares(xi, D, &ss);
        float ms = ss * inv_D;
        float inv_r = 1.0f / sqrtf(ms + eps_f);
        hx_scalar_mul(xi, inv_r, oi, D);
        hx_vmul(oi, g, oi, D);
    }
}

// ─────────────────────────────────────────────────────────────
// RMSNorm backward — matches hxvdsp_rmsnorm_bwd exactly
// ─────────────────────────────────────────────────────────────
void hxvdsp_rmsnorm_bwd(int64_t S, int64_t D,
                        int64_t x_ptr, int64_t gain_ptr, int64_t dy_ptr,
                        double eps,
                        int64_t dx_ptr, int64_t dg_ptr) {
    const float* x  = (const float*)(uintptr_t)x_ptr;
    const float* g  = (const float*)(uintptr_t)gain_ptr;
    const float* dy = (const float*)(uintptr_t)dy_ptr;
    float* dx = (float*)(uintptr_t)dx_ptr;
    float* dg = (float*)(uintptr_t)dg_ptr;
    const float eps_f = (float)eps;
    const float inv_D = 1.0f / (float)D;

    float* scratch = (float*)malloc((size_t)D * sizeof(float));
    float* term1   = (float*)malloc((size_t)D * sizeof(float));

    for (int64_t i = 0; i < S; i++) {
        const float* xi  = x  + i * D;
        const float* dyi = dy + i * D;
        float* dxi       = dx + i * D;
        float ss = 0.0f;
        hx_sum_of_squares(xi, D, &ss);
        float ms = ss * inv_D;
        float r = sqrtf(ms + eps_f);
        float inv_r = 1.0f / r;
        float inv_r3 = inv_r * inv_r * inv_r;

        // coef = Σ x*g*dy
        hx_vmul(xi, g, scratch, D);
        float coef = 0.0f;
        for (int64_t p = 0; p < D; p++) coef += scratch[p] * dyi[p];

        // term1 = g * dy * inv_r
        hx_vmul(g, dyi, term1, D);
        hx_scalar_mul(term1, inv_r, term1, D);

        // scratch = x * (coef * inv_r3 / D)
        float k = coef * inv_r3 * inv_D;
        hx_scalar_mul(xi, k, scratch, D);

        // dxi = term1 - scratch   (vDSP_vsub semantics: C = B - A)
        hx_vsub(scratch, term1, dxi, D);

        // dg += x * dy * inv_r
        hx_vmul(xi, dyi, scratch, D);
        hx_scalar_mul(scratch, inv_r, scratch, D);
        hx_vadd(dg, scratch, dg, D);
    }
    free(scratch);
    free(term1);
}

// ─────────────────────────────────────────────────────────────
// Softmax row (in-place), starting at row_offset_f32 floats.
// Mirrors hxvdsp_softmax_row signature exactly.
// ─────────────────────────────────────────────────────────────
void hxvdsp_softmax_row(int64_t x_ptr, int64_t row_offset_f32, int64_t n) {
    float* x = ((float*)(uintptr_t)x_ptr) + row_offset_f32;
    float mx = hx_max(x, n);
    float neg_mx = -mx;
    hx_scalar_add(x, neg_mx, x, n);
    hx_expf_vec(x, n);
    float sum = hx_sum(x, n);
    float inv = 1.0f / sum;
    hx_scalar_mul(x, inv, x, n);
}

// ─────────────────────────────────────────────────────────────
// SwiGLU-self forward — out[i] = up[i] * sigmoid(up[i])
// ─────────────────────────────────────────────────────────────
void hxvdsp_swiglu_fwd(int64_t n,
                       int64_t up_ptr, int64_t out_ptr) {
    const float* up = (const float*)(uintptr_t)up_ptr;
    float* out = (float*)(uintptr_t)out_ptr;

    // out = sigmoid(up) = 1/(1 + exp(-up))
    hx_scalar_mul(up, -1.0f, out, n);  // out = -up
    hx_expf_vec(out, n);               // out = exp(-up)
    hx_scalar_add(out, 1.0f, out, n);  // out = 1 + exp(-up)
    hx_recf_vec(out, n);               // out = sigmoid
    hx_vmul(up, out, out, n);          // out = up * sigmoid
}

// ─────────────────────────────────────────────────────────────
// SwiGLU-self backward
//   dx[i] = dy[i] * (s + up[i]*s*(1-s))   where s = sigmoid(up[i])
// ─────────────────────────────────────────────────────────────
void hxvdsp_swiglu_bwd(int64_t n,
                       int64_t up_ptr, int64_t dy_ptr, int64_t dx_ptr) {
    const float* up = (const float*)(uintptr_t)up_ptr;
    const float* dy = (const float*)(uintptr_t)dy_ptr;
    float* dx = (float*)(uintptr_t)dx_ptr;

    float* s    = (float*)malloc((size_t)n * sizeof(float));
    float* tmp  = (float*)malloc((size_t)n * sizeof(float));

    // s = sigmoid(up)
    hx_scalar_mul(up, -1.0f, s, n);
    hx_expf_vec(s, n);
    hx_scalar_add(s, 1.0f, s, n);
    hx_recf_vec(s, n);

    // tmp = up * s
    hx_vmul(up, s, tmp, n);
    // dx = tmp * s  (= up*s²)
    hx_vmul(tmp, s, dx, n);
    // tmp = tmp - dx  (= up*s - up*s² = up*s*(1-s))
    hx_vsub(dx, tmp, tmp, n);
    // s = s + tmp  (= s + up*s*(1-s))
    hx_vadd(s, tmp, s, n);
    // dx = dy * s
    hx_vmul(dy, s, dx, n);

    free(s);
    free(tmp);
}
