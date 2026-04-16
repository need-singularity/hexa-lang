// hxlayer_linux.c — hexa FFI ↔ fused layer-level kernels (Linux x86_64)
//
// C2 roadmap — "layer-level fused FFI" Step 1 (minimum shippable increment).
// This file is the entry point for fusing 2+ eltwise ops into a single C
// kernel so the Hexa-side incurs one dispatch instead of N, and so the
// intermediate buffer between ops never hits DRAM.
//
// Step 1 kernel: hxlayer_rmsnorm_silu
//   y = silu( rmsnorm(x, gamma, eps) )
//
// Port baseline: the two source ops live in self/native/hxblas_linux.c:
//   * hxblas_rmsnorm_fwd  (S, D, eps, y, x, g)         → reads x, gamma → y
//   * hxblas_silu_fwd     (N, out, x)                   → reads x → out
// The fused kernel combines them into one streaming loop per row:
//   row i:  compute r = 1/sqrt(mean(x_i²) + eps)
//           for p: tmp = x_i[p] * r * gamma[p]
//                  y_i[p] = tmp * sigmoid(tmp)
// No intermediate buffer between rmsnorm output and silu input — the
// tmp is carried in a register (scalar) across the inner combined pass.
//
// Build (Linux x86_64, Ubuntu/Debian):
//   bash scripts/build_hxlayer_linux.sh
// Cross-verify (Mac):
//   bash scripts/build_hxlayer_linux.sh --mac-xverify
//
// Deployment on H100 pod:
//   cp self/native/build/libhxlayer.so /usr/local/lib/libhxlayer.so
//   ldconfig          # OR set LD_LIBRARY_PATH=self/native/build
//   # hexa source points to @link("hxlayer") → libhxlayer.so resolved
//
// ABI convention (matches hxblas_linux.c bit-for-bit):
//   * pointers are passed as int64_t (Hexa Pointer == uint64_t opaque)
//   * floats use C float* (f32) — dims and strides are int64_t
//   * eps scalar is double on the hexa side, cast to float internally
//
// .hexanoport marker convention:
//   This .c is a native shim (extern FFI boundary), not a compilation
//   target for the Hexa-to-C codegen. See hexa_cc.c.hexanoport.

#ifdef __linux__

#include <math.h>
#include <stdint.h>
#include <stddef.h>

// ─────────────────────────────────────────────────────────────
// Fused RMSNorm + SiLU — single streaming kernel
//
// Shape contract:
//   x     : [N]  input row (float32)
//   gamma : [N]  per-channel scale (float32)
//   out   : [N]  output (float32)
//   eps   : scalar (passed as double, used as float)
//
// For this Step 1 increment the row count is fixed at 1 (caller loops
// over S rows externally if needed). Keeping N scalar keeps the API
// minimal and matches the "one fused kernel" target.
// ─────────────────────────────────────────────────────────────
void hxlayer_rmsnorm_silu(int64_t N, int64_t out, int64_t x,
                          int64_t gamma, double eps) {
    float* o = (float*)(uintptr_t)out;
    const float* X = (const float*)(uintptr_t)x;
    const float* G = (const float*)(uintptr_t)gamma;
    const float epsf = (float)eps;
    const float invN = 1.0f / (float)N;

    // Pass 1: compute mean-square. Fused kernel still needs 2 passes
    // over x because rmsnorm is non-local (needs the row sum). But the
    // intermediate rmsnorm output never materialises in memory — pass 2
    // produces silu(tmp) directly.
    float ms = 0.0f;
    for (int64_t p = 0; p < N; p++) ms += X[p] * X[p];
    ms *= invN;
    const float inv_r = 1.0f / sqrtf(ms + epsf);

    // Pass 2: combined rmsnorm + silu. The rmsnorm scalar `t` is carried
    // in a register straight into the silu — no intermediate buffer.
    for (int64_t p = 0; p < N; p++) {
        float t = X[p] * inv_r * G[p];
        float s = 1.0f / (1.0f + expf(-t));
        o[p] = t * s;
    }
}

// ─────────────────────────────────────────────────────────────
// ABI version probe — matches hxblas_version / hxvdsp_version style.
// Start at 1; bump on any breaking signature change.
// ─────────────────────────────────────────────────────────────
int64_t hxlayer_version(void) {
    return 1;
}

#endif /* __linux__ */
