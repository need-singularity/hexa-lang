// hxlmhead_linux.c — hexa FFI ↔ fused LM-head backward kernel (v0 stub)
//
// C-next roadmap — recommended by the 2026-04-16 CLM kernel audit
// (/tmp/clm_next_kernel_audit.md, rank #1). Replaces the two pure-hexa
// triple-loop GEMMs that dominate train_clm.hexa:1056-1071 (dL_dmid =
// dL_dlogits @ V^T) and 1089-1104 (dL_dx = dL_dmid @ U^T), folding in
// the cross-entropy forward+backward as well.
//
// v0 STATUS: this is a STUB — the implementation is correct (naive
// forward-recompute + naive triple-loop sgemms) but DELIBERATELY
// unoptimised. Goal: ship the ABI + build infra so anima can wire
// against it now; Day-2 flips the inner sgemms to libhxblas calls
// (or fuses them through reserved0). The naive path still gives a
// correctness oracle for the optimised version.
//
// v0 kernel: hxlmhead_bwd
//   inputs : x[S,H], mid[S,M] (or recomputed), targets[S], scale,
//            Wu[H,M], Wv[M,V]
//   compute (forward recompute):
//     mid    = x @ Wu                    [S, M]
//     logits = mid @ Wv                  [S, V]
//     prob   = softmax(logits, axis=-1)  [S, V]
//     loss   = -mean(log(prob[i, targets[i]])) * scale
//   backward:
//     dlogits = (prob - onehot(targets)) * scale / S            [S, V]
//     dWv += mid^T @ dlogits                                   [M, V]
//     dmid = dlogits @ Wv^T                                    [S, M]
//     dWu += x^T @ dmid                                        [H, M]
//     dx   = dmid @ Wu^T                                       [S, H]
//
// ABI (struct-passing, since hexa FFI tops out at 6 positional args):
//   hxlmhead_bwd(args_p)
//     args_p is a pointer to HxLMHeadArgs (packed by hexa via
//     alloc_raw + ptr_write/write_f32, see self/ml/hxlmhead.hexa).
//   The single-pointer signature keeps the FFI surface inside the
//   6-arg ceiling (host_ffi_call_6 in self/runtime.c) and gives us a
//   stable boundary for adding fields without breaking callers.
//
// Day-2 evolution path (RESERVED in args.reserved0):
//   bit 0 : pass mid_p instead of recomputing (skip x@Wu in forward)
//   bit 1 : zero dW_u/dW_v before accumulating (default: caller zeros)
//   bit 2 : route inner sgemms through libhxblas (drop naive loops)
//   bits 3+ : reserved
//
// Build (Linux x86_64, Ubuntu/Debian):
//   bash scripts/build_hxlmhead_linux.sh
// Cross-verify (Mac):
//   bash scripts/build_hxlmhead_linux.sh --mac-xverify
// Mac-native live FFI:
//   bash scripts/build_hxlmhead_linux.sh --mac-native
//
// .hexanoport marker convention:
//   This .c is a native shim (extern FFI boundary), not a compilation
//   target for the Hexa-to-C codegen. See hexa_cc.c.hexanoport.

#ifdef __linux__

#include <cblas.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#else /* macOS / Darwin */

#include <Accelerate/Accelerate.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#endif /* platform headers */

// ─────────────────────────────────────────────────────────────
// Packed argument struct. Field order is ABI; do not reorder.
// New fields go BEFORE reserved0 OR consume reserved bits.
// All pointers stored as int64_t for binary-clean hexa marshalling.
// ─────────────────────────────────────────────────────────────
typedef struct HxLMHeadArgs {
    int64_t S, H, M, V;          // dims: seq, hidden, mid (rank), vocab
    double  loss_scale;           // scalar multiplier on loss + grads
    int64_t x_p;                  // float32* [S, H]   input activation
    int64_t mid_p;                // float32* [S, M]   (recomputed if 0)
    int64_t targets_p;            // int32*   [S]      class indices
    int64_t Wu_p;                 // float32* [H, M]   first projection
    int64_t Wv_p;                 // float32* [M, V]   vocab projection
    int64_t dL_dx_p;              // float32* [S, H]   OUT: dL/dx
    int64_t dW_u_p;               // float32* [H, M]   OUT: accumulator
    int64_t dW_v_p;               // float32* [M, V]   OUT: accumulator
    int64_t loss_out_p;           // float64* [1]      OUT: scalar loss
    int64_t reserved0;            // Day-2 fused flags (see bit map above)
} HxLMHeadArgs;

// ─────────────────────────────────────────────────────────────
// hxlmhead_version — bump on any breaking signature change.
// v0 = struct-based ABI, naive triple-loop body.
// ─────────────────────────────────────────────────────────────
int64_t hxlmhead_version(void) {
    return 1;
}

// ─────────────────────────────────────────────────────────────
// BLAS-accelerated sgemm wrappers (ROI-2: replaces naive triple-loop).
// Linux: OpenBLAS cblas_sgemm. Mac: Accelerate cblas_sgemm.
// All use CblasRowMajor matching the [row, col] C-contiguous layout.
// ─────────────────────────────────────────────────────────────

// C[m,n] = A[m,k] @ B[k,n]   (overwrite, beta=0)
static void blas_matmul(const float* A, const float* B, float* C,
                        int64_t M_, int64_t K_, int64_t N_) {
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                (int)M_, (int)N_, (int)K_,
                1.0f,
                A, (int)K_,
                B, (int)N_,
                0.0f,
                C, (int)N_);
}

// C[m,n] += A^T[m,k] @ B[k,n]   (accumulate, beta=1)
// A is stored [K,M] row-major; CblasTrans reads columns as rows.
static void blas_matmul_TN_acc(const float* A, const float* B, float* C,
                               int64_t M_, int64_t K_, int64_t N_) {
    cblas_sgemm(CblasRowMajor, CblasTrans, CblasNoTrans,
                (int)M_, (int)N_, (int)K_,
                1.0f,
                A, (int)M_,
                B, (int)N_,
                1.0f,
                C, (int)N_);
}

// C[m,n] = A[m,k] @ B^T[k,n]   (overwrite, beta=0)
// B is stored [N,K] row-major; CblasTrans reads columns as rows.
static void blas_matmul_NT(const float* A, const float* B, float* C,
                           int64_t M_, int64_t K_, int64_t N_) {
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                (int)M_, (int)N_, (int)K_,
                1.0f,
                A, (int)K_,
                B, (int)K_,
                0.0f,
                C, (int)N_);
}

// ─────────────────────────────────────────────────────────────
// hxlmhead_bwd — fused LM-head backward (v0 stub: naive impl)
//
// Returns: 0 on success, negative on error.
//   -1 : null args pointer
//   -2 : invalid dims (any of S,H,M,V <= 0)
//   -3 : null required input pointer
//   -4 : malloc failure for scratch buffers
// ─────────────────────────────────────────────────────────────
int64_t hxlmhead_bwd(int64_t args_p) {
    if (args_p == 0) return -1;
    HxLMHeadArgs* a = (HxLMHeadArgs*)(uintptr_t)args_p;
    const int64_t S = a->S, H = a->H, M = a->M, V = a->V;
    if (S <= 0 || H <= 0 || M <= 0 || V <= 0) return -2;

    const float* x        = (const float*)(uintptr_t)a->x_p;
    const int32_t* tgt    = (const int32_t*)(uintptr_t)a->targets_p;
    const float* Wu       = (const float*)(uintptr_t)a->Wu_p;
    const float* Wv       = (const float*)(uintptr_t)a->Wv_p;
    float* dL_dx          = (float*)(uintptr_t)a->dL_dx_p;
    float* dW_u           = (float*)(uintptr_t)a->dW_u_p;
    float* dW_v           = (float*)(uintptr_t)a->dW_v_p;
    double* loss_out      = (double*)(uintptr_t)a->loss_out_p;
    if (!x || !tgt || !Wu || !Wv || !dL_dx || !dW_u || !dW_v) return -3;

    const float scale = (float)a->loss_scale;

    // Scratch: mid[S,M], logits[S,V] (== prob in-place), dmid[S,M].
    float* mid    = (float*)malloc(sizeof(float) * (size_t)S * (size_t)M);
    float* logits = (float*)malloc(sizeof(float) * (size_t)S * (size_t)V);
    float* dmid   = (float*)malloc(sizeof(float) * (size_t)S * (size_t)M);
    if (!mid || !logits || !dmid) {
        free(mid); free(logits); free(dmid);
        return -4;
    }

    // Forward recompute: mid = x @ Wu
    // (Day-2: skip when reserved0 bit0 set and a->mid_p != 0.)
    blas_matmul(x, Wu, mid, S, H, M);

    // Forward: logits = mid @ Wv
    blas_matmul(mid, Wv, logits, S, M, V);

    // Forward: softmax row-wise (in place) + CE loss aggregation.
    double loss_sum = 0.0;
    for (int64_t i = 0; i < S; i++) {
        float* row = logits + i * V;
        float mx = row[0];
        for (int64_t j = 1; j < V; j++) if (row[j] > mx) mx = row[j];
        float sum_e = 0.0f;
        for (int64_t j = 0; j < V; j++) {
            row[j] = expf(row[j] - mx);
            sum_e += row[j];
        }
        const float inv = 1.0f / sum_e;
        for (int64_t j = 0; j < V; j++) row[j] *= inv;

        const int32_t t = tgt[i];
        const int64_t ti = (t >= 0 && t < V) ? (int64_t)t : 0;
        const float pt = row[ti];
        loss_sum += -log((double)(pt > 1e-30f ? pt : 1e-30f));
    }
    if (loss_out) *loss_out = (loss_sum / (double)S) * (double)a->loss_scale;

    // Backward: dlogits = (prob - onehot(t)) * scale / S, in place.
    const float inv_S = 1.0f / (float)S;
    for (int64_t i = 0; i < S; i++) {
        float* row = logits + i * V;     // == prob
        for (int64_t j = 0; j < V; j++) row[j] *= scale * inv_S;
        const int32_t t = tgt[i];
        const int64_t ti = (t >= 0 && t < V) ? (int64_t)t : 0;
        row[ti] -= scale * inv_S;
    }
    // (logits now holds dlogits.)

    // dWv += mid^T @ dlogits   ([M,V] += [S,M]^T @ [S,V])
    blas_matmul_TN_acc(mid, logits, dW_v, M, S, V);

    // dmid = dlogits @ Wv^T   ([S,M] = [S,V] @ [M,V]^T)
    blas_matmul_NT(logits, Wv, dmid, S, V, M);

    // dWu += x^T @ dmid   ([H,M] += [S,H]^T @ [S,M])
    blas_matmul_TN_acc(x, dmid, dW_u, H, S, M);

    // dx = dmid @ Wu^T   ([S,H] = [S,M] @ [H,M]^T)
    blas_matmul_NT(dmid, Wu, dL_dx, S, M, H);

    free(mid); free(logits); free(dmid);
    return 0;
}
