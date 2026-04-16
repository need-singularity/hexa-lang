// hxflash_linux.c — hexa FFI ↔ fused Flash Attention kernel (Linux x86_64)
//
// C8 roadmap — "Flash Attention hexa native" Day-1 MVP. Companion to
// hxblas_linux.c (GEMM) and hxlayer_linux.c (fused rmsnorm+silu). This
// file is the entry point for a tile-wise online-softmax attention
// kernel so the Hexa side avoids (a) materialising the O(S²) score
// matrix and (b) issuing one FFI dispatch per tile.
//
// Day-1 kernel: hxflash_attn_fwd
//   forward-only, non-causal (causal=0); causal=1 is a Day-2 stub.
//
// Port baseline: the reference implementation lives in pure hexa at
//   self/ai_native/flash_attention.hexa :: flash_attention(Q,K,V,Br,Bc)
// (lines 188-279). This is a direct transliteration into C, preserving
// the online-softmax recurrence (m, l) and per-tile rescale.
//
// Build (Linux x86_64, Ubuntu/Debian):
//   hexa scripts/build_hxflash_linux.hexa
// Cross-verify (Mac):
//   hexa scripts/build_hxflash_linux.hexa --mac-xverify
// Mac-native live FFI:
//   hexa scripts/build_hxflash_linux.hexa --mac-native
//
// ABI convention (matches hxblas_linux.c / hxlayer_linux.c bit-for-bit):
//   * pointers are passed as int64_t (Hexa Pointer == uint64_t opaque)
//   * floats use C float* (f32) — dims and strides are int64_t
//   * scale is double on the hexa side, cast to float internally
//
// Layout (Day-1):
//   Q, K, V, O are flat float32 buffers, contiguous [n_heads, S, D]
//   stored in head-major order (head h row i chan k → base_* + i*D + k).
//
//   Q stride = n_heads * D per head
//   K/V stride = n_kv_heads * D per head  (GQA aware)
//
//   For grouped-query attention (GQA), each Q head h maps to a KV head:
//     kv_h = h / (n_heads / n_kv_heads)
//
//   For Day-1 we require n_heads == n_kv_heads (no-GQA) OR the caller
//   has already replicated KV heads — we honour the mapping regardless.
//
// .hexanoport marker convention:
//   This .c is a native shim (extern FFI boundary), not a compilation
//   target for the Hexa-to-C codegen. See hexa_cc.c.hexanoport.

#ifdef __linux__

#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

// ─────────────────────────────────────────────────────────────
// Scalar helpers. Kept out-of-line so -O3 can decide inlining.
// ─────────────────────────────────────────────────────────────
static inline float hxf_maxf(float a, float b) { return a > b ? a : b; }
static inline int64_t hxf_mini(int64_t a, int64_t b) { return a < b ? a : b; }

// ─────────────────────────────────────────────────────────────
// hxflash_attn_fwd — forward-only, online-softmax tiled attention
//
// Signature (matches the .hexa extern binding in self/ml/hxflash.hexa):
//   void hxflash_attn_fwd(int64_t S, int64_t D,
//                         int64_t n_heads, int64_t n_kv_heads,
//                         int64_t Br, int64_t Bc, int64_t causal,
//                         int64_t Q, int64_t K, int64_t V, int64_t O,
//                         double scale);
//
// Contract:
//   Q : [n_heads   , S, D] float32   base_q  = h   * D per row i
//   K : [n_kv_heads, S, D] float32   base_kv = kv_h * D per row j
//   V : [n_kv_heads, S, D] float32
//   O : [n_heads   , S, D] float32  (zero-init or overwritten per-row)
//
//   Tile sizes:
//     Br — query rows per tile (Day-1 cap 64)
//     Bc — key/value cols per tile (Day-1 cap 64)
//
//   scale is typically 1/sqrt(D) but the caller owns the choice so the
//   kernel stays head-dim agnostic.
//
//   causal=0 : non-causal (Day-1 target)
//   causal=1 : Day-2 — currently masks (gk > gq) to -inf and skips
//              fully-masked rows, matching flash_attention_causal in
//              self/ai_native/flash_attention.hexa for reference.
//
// Memory plan (per query tile, per Q head):
//   Scratch  =  S_tile[Br*Bc]  +  row_m[Br]  +  row_l[Br]
//             + O_block[Br*D]  +  p_row[Bc]
//   At Br=Bc=64, D=128 :  64*64 + 64 + 64 + 64*128 + 64 = ~12.5KB
//   At Br=Bc=64, D=256 :  ~20KB — still comfortably on the stack.
// ─────────────────────────────────────────────────────────────
void hxflash_attn_fwd(int64_t S, int64_t D,
                      int64_t n_heads, int64_t n_kv_heads,
                      int64_t Br, int64_t Bc, int64_t causal,
                      int64_t Q, int64_t K, int64_t V, int64_t O,
                      double scale) {
    const float* Qp = (const float*)(uintptr_t)Q;
    const float* Kp = (const float*)(uintptr_t)K;
    const float* Vp = (const float*)(uintptr_t)V;
    float*       Op = (float*      )(uintptr_t)O;
    const float  sc = (float)scale;
    const float  NEG_INF = -1.0e30f;

    if (S <= 0 || D <= 0 || n_heads <= 0 || n_kv_heads <= 0) return;
    if (Br <= 0) Br = 64;
    if (Bc <= 0) Bc = 64;
    if (Br > S)  Br = S;
    if (Bc > S)  Bc = S;

    // GQA divisor — how many Q heads share one KV head.
    const int64_t group = n_heads / n_kv_heads;
    const int64_t q_head_stride  = S * D;           // per Q-head offset
    const int64_t kv_head_stride = S * D;           // per KV-head offset

    const int64_t nr = (S + Br - 1) / Br;
    const int64_t nc = (S + Bc - 1) / Bc;

    // Heap-allocate the largest per-row scratch once. Br*D can be 64*256
    // = 16KB — still stack-safe but heap keeps the function re-entrant
    // without VLA concerns under strict portability.
    float* scratch = (float*)malloc(sizeof(float) *
        ((size_t)Br * (size_t)Bc                // S_tile
       + (size_t)Br                              // row_m
       + (size_t)Br                              // row_l
       + (size_t)Br * (size_t)D                  // O_block
       + (size_t)Bc));                           // p_row
    if (!scratch) return;

    float* S_tile  = scratch;
    float* row_m   = S_tile  + (size_t)Br * (size_t)Bc;
    float* row_l   = row_m   + (size_t)Br;
    float* O_block = row_l   + (size_t)Br;
    float* p_row   = O_block + (size_t)Br * (size_t)D;

    for (int64_t h = 0; h < n_heads; h++) {
        const int64_t kv_h   = (group > 0) ? (h / group) : h;
        const int64_t base_q = h    * q_head_stride;
        const int64_t base_kv = kv_h * kv_head_stride;

        for (int64_t bq = 0; bq < nr; bq++) {
            const int64_t q_start = bq * Br;
            const int64_t q_end   = hxf_mini(q_start + Br, S);
            const int64_t rows    = q_end - q_start;

            for (int64_t qi = 0; qi < rows; qi++) {
                row_m[qi] = NEG_INF;
                row_l[qi] = 0.0f;
            }
            memset(O_block, 0, (size_t)rows * (size_t)D * sizeof(float));

            for (int64_t bkv = 0; bkv < nc; bkv++) {
                const int64_t kv_start = bkv * Bc;
                const int64_t kv_end   = hxf_mini(kv_start + Bc, S);
                const int64_t cols     = kv_end - kv_start;

                // S_tile = Q_block · K_block^T · scale   (rows × cols)
                for (int64_t qi = 0; qi < rows; qi++) {
                    const int64_t gq = q_start + qi;
                    const float*  qrow = Qp + base_q + gq * D;
                    for (int64_t kj = 0; kj < cols; kj++) {
                        const int64_t gk = kv_start + kj;
                        if (causal && gk > gq) {
                            S_tile[qi * Bc + kj] = NEG_INF;
                        } else {
                            const float* krow = Kp + base_kv + gk * D;
                            float dot = 0.0f;
                            for (int64_t k = 0; k < D; k++) {
                                dot += qrow[k] * krow[k];
                            }
                            S_tile[qi * Bc + kj] = dot * sc;
                        }
                    }
                }

                // per-row online softmax update
                for (int64_t qi = 0; qi < rows; qi++) {
                    // Skip fully-masked rows (causal upper-right tiles).
                    if (causal) {
                        int all_masked = 1;
                        for (int64_t kj = 0; kj < cols; kj++) {
                            if (S_tile[qi * Bc + kj] > -1.0e29f) {
                                all_masked = 0;
                                break;
                            }
                        }
                        if (all_masked) continue;
                    }

                    float tile_max = S_tile[qi * Bc + 0];
                    for (int64_t kj = 1; kj < cols; kj++) {
                        float v = S_tile[qi * Bc + kj];
                        if (v > tile_max) tile_max = v;
                    }

                    const float m_old = row_m[qi];
                    const float m_new = hxf_maxf(m_old, tile_max);
                    const float alpha = expf(m_old - m_new);

                    float tile_sum = 0.0f;
                    for (int64_t kj = 0; kj < cols; kj++) {
                        const float p = expf(S_tile[qi * Bc + kj] - m_new);
                        p_row[kj] = p;
                        tile_sum += p;
                    }

                    const float l_new = row_l[qi] * alpha + tile_sum;

                    // Rescale prior accumulator: O_block[qi, :] *= alpha
                    float* obrow = O_block + (size_t)qi * (size_t)D;
                    for (int64_t k = 0; k < D; k++) obrow[k] *= alpha;

                    // Add P · V_block
                    for (int64_t kj = 0; kj < cols; kj++) {
                        const int64_t gk = kv_start + kj;
                        const float   p  = p_row[kj];
                        const float*  vrow = Vp + base_kv + gk * D;
                        for (int64_t k = 0; k < D; k++) {
                            obrow[k] += p * vrow[k];
                        }
                    }

                    row_m[qi] = m_new;
                    row_l[qi] = l_new;
                }
            }

            // Final normalise: O[qi, :] /= l[qi]
            for (int64_t qi = 0; qi < rows; qi++) {
                const int64_t gq = q_start + qi;
                float* out_row = Op + base_q + gq * D;
                const float inv_l = (row_l[qi] > 0.0f)
                                  ? (1.0f / row_l[qi])
                                  : 0.0f;
                const float* obrow = O_block + (size_t)qi * (size_t)D;
                for (int64_t k = 0; k < D; k++) {
                    out_row[k] = obrow[k] * inv_l;
                }
            }
        }
    }

    free(scratch);
}

// ─────────────────────────────────────────────────────────────
// Struct-args ABI (2026-04-16 C8 fix)
//
// The 12-positional-arg hxflash_attn_fwd above exceeds the Hexa
// interpreter's host_ffi_call_6 6-slot ceiling (see self/runtime.c
// hexa_host_ffi_call_6 — args 7+ arrive uninitialised). The fix
// mirrors the hxlmhead_bwd pattern: pack all 12 args into a heap
// HxFlashArgs struct and pass a single int64 pointer through FFI.
//
// Field order is ABI; do not reorder. New fields go BEFORE reserved0
// OR consume reserved bits. All pointers stored as int64_t for
// binary-clean hexa marshalling (ptr_write writes int as 8 bytes).
// ─────────────────────────────────────────────────────────────
typedef struct HxFlashArgs {
    int64_t S, D;                  // seq_len, head_dim
    int64_t n_heads, n_kv_heads;   // GQA dims
    int64_t Br, Bc;                // block tiling sizes
    int64_t causal;                // bool flag (0/1)
    int64_t q_p, k_p, v_p, o_p;    // float32* buffers
    double  scale;                 // attention scale (1/sqrt(D))
    int64_t reserved0;             // future flags
} HxFlashArgs;

// hxflash_attn_fwd_packed — single-pointer entry point for Hexa FFI.
// Unpacks HxFlashArgs and delegates to the existing 12-arg kernel.
// Returns: 0 on success, negative on error.
//   -1 : null args pointer
int64_t hxflash_attn_fwd_packed(int64_t args_p) {
    if (args_p == 0) return -1;
    HxFlashArgs* a = (HxFlashArgs*)(uintptr_t)args_p;
    hxflash_attn_fwd(a->S, a->D,
                     a->n_heads, a->n_kv_heads,
                     a->Br, a->Bc, a->causal,
                     a->q_p, a->k_p, a->v_p, a->o_p,
                     a->scale);
    return 0;
}

// ─────────────────────────────────────────────────────────────
// hxflash_attn_bwd — tiled backward pass (Dao 2022)
//
// Gradient formulae per row i:
//   D_i    = <dO_i, O_i>
//   P_ij   = exp(Q_i·K_j·scale − m_i) / l_i   (recomputed)
//   dS_ij  = P_ij · (<dO_i, V_j> − D_i)
//   dQ_i  += sum_j dS_ij · K_j · scale
//   dK_j  += sum_i dS_ij · Q_i · scale
//   dV_j  += sum_i P_ij  · dO_i
//
// Two sweeps per query block:
//   Sweep 1: forward stats (m, l, O) via online softmax
//   Sweep 2: recompute P, accumulate dQ/dK/dV
//
// Memory: O(Br*Bc + Br*D) scratch — no S^2 materialisation.
// ─────────────────────────────────────────────────────────────

typedef struct HxFlashBwdArgs {
    int64_t S, D;
    int64_t n_heads, n_kv_heads;
    int64_t Br, Bc;
    int64_t causal;
    int64_t q_p, k_p, v_p, o_p;   // saved fwd output O
    int64_t dO_p;                   // grad of output
    int64_t dQ_p, dK_p, dV_p;      // output grad buffers
    double  scale;
    int64_t reserved0;
} HxFlashBwdArgs;

// HxFlashBwdArgs field offsets (bytes) — must match hexa side:
//   0:S  8:D  16:n_heads  24:n_kv_heads  32:Br  40:Bc  48:causal
//  56:q_p  64:k_p  72:v_p  80:o_p  88:dO_p
//  96:dQ_p  104:dK_p  112:dV_p  120:scale  128:reserved0
// total = 136 bytes

static void hxflash_attn_bwd(int64_t S, int64_t D,
                              int64_t n_heads, int64_t n_kv_heads,
                              int64_t Br, int64_t Bc, int64_t causal,
                              const float* Qp, const float* Kp,
                              const float* Vp, const float* Op,
                              const float* dOp,
                              float* dQp, float* dKp, float* dVp,
                              float scale_f) {
    const float NEG_INF = -1.0e30f;

    if (S <= 0 || D <= 0 || n_heads <= 0 || n_kv_heads <= 0) return;
    if (Br <= 0) Br = 64;
    if (Bc <= 0) Bc = 64;
    if (Br > S)  Br = S;
    if (Bc > S)  Bc = S;

    const int64_t group = n_heads / n_kv_heads;
    const int64_t q_head_stride  = S * D;
    const int64_t kv_head_stride = S * D;

    const int64_t nr = (S + Br - 1) / Br;
    const int64_t nc = (S + Bc - 1) / Bc;

    // Scratch per query block:
    //   S_tile[Br*Bc] + row_m[Br] + row_l[Br] + O_block[Br*D]
    //   + p_row[Bc] + row_D[Br] + dQ_block[Br*D]
    size_t scratch_sz = (size_t)Br * (size_t)Bc   // S_tile
                      + (size_t)Br                  // row_m
                      + (size_t)Br                  // row_l
                      + (size_t)Br * (size_t)D      // O_block
                      + (size_t)Bc                  // p_row
                      + (size_t)Br                  // row_D
                      + (size_t)Br * (size_t)D;     // dQ_block
    float* scratch = (float*)malloc(sizeof(float) * scratch_sz);
    if (!scratch) return;

    float* S_tile   = scratch;
    float* row_m    = S_tile   + (size_t)Br * (size_t)Bc;
    float* row_l    = row_m    + (size_t)Br;
    float* O_block  = row_l    + (size_t)Br;
    float* p_row    = O_block  + (size_t)Br * (size_t)D;
    float* row_D    = p_row    + (size_t)Bc;
    float* dQ_block = row_D    + (size_t)Br;

    // Zero output grads (caller may not have zeroed them)
    memset(dQp, 0, (size_t)n_heads    * (size_t)S * (size_t)D * sizeof(float));
    memset(dKp, 0, (size_t)n_kv_heads * (size_t)S * (size_t)D * sizeof(float));
    memset(dVp, 0, (size_t)n_kv_heads * (size_t)S * (size_t)D * sizeof(float));

    for (int64_t h = 0; h < n_heads; h++) {
        const int64_t kv_h    = (group > 0) ? (h / group) : h;
        const int64_t base_q  = h    * q_head_stride;
        const int64_t base_kv = kv_h * kv_head_stride;

        for (int64_t bq = 0; bq < nr; bq++) {
            const int64_t q_start = bq * Br;
            const int64_t q_end   = hxf_mini(q_start + Br, S);
            const int64_t rows    = q_end - q_start;

            // ── Sweep 1: forward stats (m, l, O) via online softmax ──
            for (int64_t qi = 0; qi < rows; qi++) {
                row_m[qi] = NEG_INF;
                row_l[qi] = 0.0f;
            }
            memset(O_block, 0, (size_t)rows * (size_t)D * sizeof(float));

            for (int64_t bkv = 0; bkv < nc; bkv++) {
                const int64_t kv_start = bkv * Bc;
                const int64_t kv_end   = hxf_mini(kv_start + Bc, S);
                const int64_t cols     = kv_end - kv_start;

                for (int64_t qi = 0; qi < rows; qi++) {
                    const int64_t gq = q_start + qi;
                    const float* qrow = Qp + base_q + gq * D;
                    for (int64_t kj = 0; kj < cols; kj++) {
                        const int64_t gk = kv_start + kj;
                        if (causal && gk > gq) {
                            S_tile[qi * Bc + kj] = NEG_INF;
                        } else {
                            const float* krow = Kp + base_kv + gk * D;
                            float dot = 0.0f;
                            for (int64_t k = 0; k < D; k++)
                                dot += qrow[k] * krow[k];
                            S_tile[qi * Bc + kj] = dot * scale_f;
                        }
                    }
                }

                for (int64_t qi = 0; qi < rows; qi++) {
                    if (causal) {
                        int all_masked = 1;
                        for (int64_t kj = 0; kj < cols; kj++) {
                            if (S_tile[qi * Bc + kj] > -1.0e29f) {
                                all_masked = 0; break;
                            }
                        }
                        if (all_masked) continue;
                    }

                    float tile_max = S_tile[qi * Bc];
                    for (int64_t kj = 1; kj < cols; kj++) {
                        float v = S_tile[qi * Bc + kj];
                        if (v > tile_max) tile_max = v;
                    }

                    const float m_old = row_m[qi];
                    const float m_new = hxf_maxf(m_old, tile_max);
                    const float alpha = expf(m_old - m_new);

                    float tile_sum = 0.0f;
                    for (int64_t kj = 0; kj < cols; kj++) {
                        float p = expf(S_tile[qi * Bc + kj] - m_new);
                        p_row[kj] = p;
                        tile_sum += p;
                    }

                    const float l_new = row_l[qi] * alpha + tile_sum;
                    float* obrow = O_block + (size_t)qi * (size_t)D;
                    for (int64_t k = 0; k < D; k++) obrow[k] *= alpha;

                    for (int64_t kj = 0; kj < cols; kj++) {
                        const int64_t gk = kv_start + kj;
                        const float p = p_row[kj];
                        const float* vrow = Vp + base_kv + gk * D;
                        for (int64_t k = 0; k < D; k++)
                            obrow[k] += p * vrow[k];
                    }
                    row_m[qi] = m_new;
                    row_l[qi] = l_new;
                }
            }

            // Normalise O_block
            for (int64_t qi = 0; qi < rows; qi++) {
                const float inv_l = (row_l[qi] > 0.0f) ? (1.0f / row_l[qi]) : 0.0f;
                float* obrow = O_block + (size_t)qi * (size_t)D;
                for (int64_t k = 0; k < D; k++)
                    obrow[k] *= inv_l;
            }

            // ── D_i = <dO_i, O_i> ──
            for (int64_t qi = 0; qi < rows; qi++) {
                const int64_t gq = q_start + qi;
                const float* do_row = dOp + base_q + gq * D;
                const float* ob_row = O_block + (size_t)qi * (size_t)D;
                float s = 0.0f;
                for (int64_t k = 0; k < D; k++)
                    s += do_row[k] * ob_row[k];
                row_D[qi] = s;
            }

            // ── Sweep 2: recompute P per tile, accumulate dQ/dK/dV ──
            memset(dQ_block, 0, (size_t)rows * (size_t)D * sizeof(float));

            for (int64_t bkv = 0; bkv < nc; bkv++) {
                const int64_t kv_start = bkv * Bc;
                const int64_t kv_end   = hxf_mini(kv_start + Bc, S);
                const int64_t cols     = kv_end - kv_start;

                for (int64_t qi = 0; qi < rows; qi++) {
                    const int64_t gq = q_start + qi;
                    const float* qrow = Qp + base_q + gq * D;
                    const float inv_l = (row_l[qi] > 0.0f) ? (1.0f / row_l[qi]) : 0.0f;
                    const float m_i = row_m[qi];
                    const float D_i = row_D[qi];
                    const float* do_row = dOp + base_q + gq * D;
                    float* dq_row = dQ_block + (size_t)qi * (size_t)D;

                    for (int64_t kj = 0; kj < cols; kj++) {
                        const int64_t gk = kv_start + kj;

                        // Recompute P_ij
                        float P_ij;
                        if (causal && gk > gq) {
                            P_ij = 0.0f;
                        } else {
                            const float* krow = Kp + base_kv + gk * D;
                            float dot = 0.0f;
                            for (int64_t k = 0; k < D; k++)
                                dot += qrow[k] * krow[k];
                            P_ij = expf(dot * scale_f - m_i) * inv_l;
                        }

                        if (P_ij == 0.0f) continue;

                        // dP_ij = <dO_i, V_j>
                        const float* vrow = Vp + base_kv + gk * D;
                        float dP_ij = 0.0f;
                        for (int64_t k = 0; k < D; k++)
                            dP_ij += do_row[k] * vrow[k];

                        // dS_ij = P_ij * (dP_ij - D_i)
                        const float dS_ij = P_ij * (dP_ij - D_i);
                        const float w = dS_ij * scale_f;

                        // dQ_i += w * K_j
                        const float* krow2 = Kp + base_kv + gk * D;
                        for (int64_t k = 0; k < D; k++)
                            dq_row[k] += w * krow2[k];

                        // dK_j += w * Q_i  (accumulate into shared dK buffer)
                        float* dk_row = dKp + base_kv + gk * D;
                        for (int64_t k = 0; k < D; k++)
                            dk_row[k] += w * qrow[k];

                        // dV_j += P_ij * dO_i
                        float* dv_row = dVp + base_kv + gk * D;
                        for (int64_t k = 0; k < D; k++)
                            dv_row[k] += P_ij * do_row[k];
                    }
                }
            }

            // Write dQ_block back to dQ
            for (int64_t qi = 0; qi < rows; qi++) {
                const int64_t gq = q_start + qi;
                float* dq_out = dQp + base_q + gq * D;
                const float* dq_src = dQ_block + (size_t)qi * (size_t)D;
                for (int64_t k = 0; k < D; k++)
                    dq_out[k] += dq_src[k];
            }
        }
    }

    free(scratch);
}

// Struct-args packed entry point for backward.
// Returns: 0 on success, -1 on null args.
int64_t hxflash_attn_bwd_packed(int64_t args_p) {
    if (args_p == 0) return -1;
    HxFlashBwdArgs* a = (HxFlashBwdArgs*)(uintptr_t)args_p;
    hxflash_attn_bwd(a->S, a->D,
                     a->n_heads, a->n_kv_heads,
                     a->Br, a->Bc, a->causal,
                     (const float*)(uintptr_t)a->q_p,
                     (const float*)(uintptr_t)a->k_p,
                     (const float*)(uintptr_t)a->v_p,
                     (const float*)(uintptr_t)a->o_p,
                     (const float*)(uintptr_t)a->dO_p,
                     (float*)(uintptr_t)a->dQ_p,
                     (float*)(uintptr_t)a->dK_p,
                     (float*)(uintptr_t)a->dV_p,
                     (float)a->scale);
    return 0;
}

// ─────────────────────────────────────────────────────────────
// ABI version probe — matches hxblas_version / hxvdsp_version /
// hxlayer_version style. Start at 1; bump on any breaking signature
// change (e.g. Day-2 causal fast path, backward pass, dtype fan-out).
// v2 adds hxflash_attn_fwd_packed (struct-args ABI).
// v3 adds hxflash_attn_bwd_packed (backward pass).
// ─────────────────────────────────────────────────────────────
int64_t hxflash_version(void) {
    return 3;
}

#endif /* __linux__ */
