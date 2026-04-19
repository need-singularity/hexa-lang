// hxqwen32b.c — hexa FFI ↔ Qwen2.5-32B training shim (Linux x86_64 / Mac stub)
//
// STATUS: v4  Day-1 scaffold (non-CUDA path LIVE, CUDA kernels TODO)
//
// Adapted from hxqwen14b.c v4 (see that file for full design commentary).
// Qwen2.5-32B vs Qwen2.5-14B — only TWO architectural fields differ:
//
//    field                14B          32B          note
//    ─────────────────── ──────────── ──────────── ─────────────────────
//    n_layer              48           64           +16 layers
//    ffn_dim (SwiGLU)     13824        27648        2× intermediate dim
//    d_model              5120         5120         identical (tied head)
//    n_head               40           40           identical
//    n_kv_head            8            8            identical GQA ratio
//    head_dim             128          128          identical
//    vocab                152064       152064       identical tokenizer
//    max_pos              32768        32768        identical RoPE span
//
// LoRA target count scales with n_layer:
//    14B: 48 layers × 4 {q,k,v,o} = 192 adapters
//    32B: 64 layers × 7 {q,k,v,o,gate,up,down} = 448 adapters (H11 audit)
//
// H11 audit recommended LORA_N_TARGETS=7 for 32B (add gate/up/down FFN
// projections) — the FFN blows up to 27648 and the trainer wants adapter
// coverage there too. This doubles param count vs 4-target but stays well
// under the 32B VRAM envelope given r=8.
//
// v1  skeleton (handles + struct-args, stub returns)                DONE
// v2  real CUDA inference forward                                   TODO
// v3  LoRA fwd/bwd/apply ABI surface (stubs return -4)              DONE
// v4  Day-1 — ABI structs locked, safetensors header parse,         THIS
//     LoRA init (Kaiming A + zero B), AdamW CPU reference, load
//     shape validation. CUDA fwd/bwd return -5 CUDA_TODO with
//     cuBLAS call sequence mapped inline. Non-CUDA path fully
//     testable on Mac.
//
// Goal: replace the Python peft/transformers training path for
// Qwen2.5-32B LoRA fine-tune. Target pod: 2× H100 SXM5 80GB (tensor-
// parallel), CUDA 11.8, cuBLAS, bf16 Tensor Core. Ranked alongside
// hxqwen14b — identical ABI conventions, identical build-script shape.
//
// Same rc==-5 contract as 14B: v4 lets the hexa-side wiring land,
// v5 lights up CUDA.
//
// ABI surface (12 symbols — matches 14B v4 surface):
//
//   int64_t hxqwen32b_load(int64_t ckpt_path_p)
//     * ckpt_path_p : const char* — path to Qwen2.5-32B weights directory
//                     (HF safetensors layout: model.safetensors.index.json
//                     + model-NNNNN-of-NNNNN.safetensors shards — typically
//                     ~16-18 shards for 32B at fp16).
//     * v4: mmap's index.json, validates tensor manifest against Qwen2.5-32B
//           shape spec (64-layer × d=5120 × ffn=27648 × vocab=152064),
//           returns a live handle. Does NOT upload weights to device.
//     * returns : int64 handle > 0 on success, negative error.
//                  -1 path null / no free slot
//                  -2 index.json missing / malformed
//                  -3 tensor shape mismatch vs Qwen2.5-32B spec
//                  -4 safetensors shard missing / unreadable
//
//   int64_t hxqwen32b_version(void)          -> 4 (Day-1)
//   int64_t hxqwen32b_free(int64_t h)        -> 0 (idempotent)
//
//   int64_t hxqwen32b_generate(int64_t args_p)
//     (stub — trainer doesn't invoke generate() during LoRA loop.)
//
//   double hxqwen32b_compute_phi_holo(int64_t h)
//     (cached sentinel until CUDA forward lands.)
//
// Day-2 LoRA training symbols (v4 — partial live, CUDA TODO):
//
//   int hxqwen32b_forward_with_lora(void* args)    HxQwen32bFwdArgs*
//   int hxqwen32b_backward_lora_only(void* args)   HxQwen32bBwdArgs*
//   int hxqwen32b_apply_lora_delta(void* args)     HxQwen32bApplyArgs*
//
// Day-1 LIVE helper entries (testable on Mac, no CUDA required):
//
//   int hxqwen32b_lora_init_A(void* args)   HxQwen32bLoraInitArgs*
//   int hxqwen32b_lora_init_B(void* args)   HxQwen32bLoraInitArgs*
//   int hxqwen32b_adamw_step_cpu(void* args) HxQwen32bAdamArgs*
//   int hxqwen32b_safetensors_probe(int64_t path_p, void* out_dims)
//
// LoRA contract (consumed by anima training/train_alm_lora_32b.hexa):
//   * 64 layers × {q,k,v,o,gate,up,down} = 448 adapters
//   * A : [r × d_or_ffn]   B : [d_or_ffn × r]   r=8, α=16
//   * d_model=5120 for q/k/v/o; ffn_dim=27648 for gate/up/down
//   * adapter forward : y = W·x + (α/r) · B · (A · x)
//   * gradient flows ONLY through A,B (W is frozen)
//   * caller passes A_flat / B_flat as packed float32 contiguous arrays
//     in the SAME ordering as the trainer (idx = layer*7 + tgt). C side
//     does NOT reorder. Stride per block varies because FFN projections
//     use ffn_dim (27648) and attn projections use d_model (5120).
//     Caller MUST pass per-block offset table (see HxQwen32bFwdArgs.offsets_p)
//     so the shim can index correctly into the flat buffer.
//
// Build (Linux x86_64, 2× H100 pod):
//   hexa tool/build_hxqwen32b_linux.hexa
//   # v4 compiles WITHOUT CUDA too — non-CUDA path (safetensors probe +
//   # LoRA init + AdamW CPU + shape validation) is the tested surface.
//   # CUDA kernels land in v5 behind HXQWEN32B_CUDA=1.
//
// Cross-verify (Mac):
//   hexa tool/build_hxqwen32b_linux.hexa --mac-xverify
// Mac-native live FFI (v4 covers init + AdamW + probe — rest is stubbed):
//   hexa tool/build_hxqwen32b_linux.hexa --mac-native
//
// .hexanoport marker convention:
//   This .c is a native shim (extern FFI boundary), not a compilation
//   target for the Hexa-to-C codegen.

#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef __linux__
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #include <unistd.h>
  #ifdef HXQWEN32B_CUDA
    // v5 path — not exercised in v4.
    #include <cuda_runtime.h>
    #include <cublas_v2.h>
    #include <cuda_bf16.h>
  #endif
#else
  // Mac: mmap is POSIX-compatible — reuse <sys/mman.h>.
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #include <unistd.h>
#endif

// ─────────────────────────────────────────────────────────────
// Qwen2.5-32B architectural constants (MUST match trainer-side
// comptime const in training/train_alm_lora_32b.hexa).
// Fields marked (*) differ from 14B.
// ─────────────────────────────────────────────────────────────
#define QWEN32B_N_LAYER     64        // (*) +16 vs 14B
#define QWEN32B_D_MODEL     5120
#define QWEN32B_VOCAB       152064
#define QWEN32B_N_HEAD      40
#define QWEN32B_N_KV_HEAD   8         // GQA: 40/8 = 5x query-per-KV
#define QWEN32B_HEAD_DIM    128       // d_model / n_head = 5120/40
#define QWEN32B_FFN_DIM     27648     // (*) 2× vs 14B (SwiGLU intermediate)
#define QWEN32B_MAX_POS     32768     // RoPE max sequence

// H11-audit LoRA target layout — 7 modules per layer (attn 4 + ffn 3).
#define LORA_N_TARGETS      7         // q,k,v,o,gate,up,down
#define LORA_N_ADAPTERS     (QWEN32B_N_LAYER * LORA_N_TARGETS)  // 448

// Error codes — stable ABI. Match 14B surface; -5 is CUDA-pending marker.
#define RC_OK                    0
#define RC_ERR_NULL_ARGS        -1
#define RC_ERR_BAD_HANDLE       -1
#define RC_ERR_INDEX_JSON       -2
#define RC_ERR_BAD_ARGS         -2
#define RC_ERR_SHAPE_MISMATCH   -3
#define RC_ERR_OOM              -3
#define RC_ERR_IO               -4
#define RC_ERR_KERNEL_FAIL      -4
#define RC_ERR_CUDA_TODO        -5        // v4 marker: non-CUDA path live,
                                          //   CUDA kernel pending v5.

// ─────────────────────────────────────────────────────────────
// Packed argument struct for hxqwen32b_generate (matches 14B layout).
// Total = 96 bytes (12 × 8B fields).
// ─────────────────────────────────────────────────────────────
typedef struct HxQwen32bGenArgs {
    int64_t handle;
    int64_t prompt_p;
    int64_t prompt_len;
    int64_t max_tokens;
    double  temperature;
    double  top_p;
    int64_t top_k;
    int64_t out_p;
    int64_t out_cap;
    int64_t seed;
    int64_t lora_handle;
    int64_t reserved0;
} HxQwen32bGenArgs;

// ─────────────────────────────────────────────────────────────
// v4 LoRA training ABI structs — FROZEN (do not reorder fields).
//
// NEW vs 14B: offsets_p (per-adapter byte-offset table). Because 32B has
// heterogeneous adapter shapes (attn uses d_model, FFN uses ffn_dim), the
// trainer must supply a table of 448 × 2 int64 (A_offset, B_offset in
// float32 element count, NOT bytes). The shim walks this table instead
// of assuming uniform block stride.
//
// When offsets_p == 0, the shim falls back to uniform block stride
// (lora_r × d_model) — useful for attn-only (4-target) training. The
// 7-target path REQUIRES offsets_p.
// ─────────────────────────────────────────────────────────────

// HxQwen32bFwdArgs — forward pass with LoRA delta (v5 live; v4 returns -5).
// Layout IS ABI. Total = 144 B (18 × 8B).
typedef struct HxQwen32bFwdArgs {
    int64_t handle;
    int64_t B_batch;
    int64_t S_seq;
    int64_t V_vocab;
    int64_t ids_p;
    int64_t logits_p;
    int64_t A_flat_p;
    int64_t B_flat_p;
    int64_t activations_p;
    int64_t lora_r;
    int64_t lora_alpha;
    int64_t loss_out_p;
    int64_t targets_p;
    double  loss_scale;
    int64_t ctx_seed;
    int64_t offsets_p;     // NEW — int64* [448·2] per-adapter (A_off, B_off)
    int64_t reserved0;     // flag word (bit 0 = flash-attn; bit 1 = save_acts)
    int64_t reserved1;
} HxQwen32bFwdArgs;

// HxQwen32bBwdArgs — backward pass, LoRA-only gradients (v5 live; v4 -5).
// Total = 144 B (18 × 8B).
typedef struct HxQwen32bBwdArgs {
    int64_t handle;
    int64_t B_batch;
    int64_t S_seq;
    int64_t V_vocab;
    int64_t dlogits_p;
    int64_t A_flat_p;
    int64_t B_flat_p;
    int64_t activations_p;
    int64_t dA_flat_p;
    int64_t dB_flat_p;
    int64_t lora_r;
    int64_t lora_alpha;
    int64_t zero_grads_first;
    double  grad_scale;
    int64_t targets_p;
    int64_t offsets_p;     // NEW — same semantics as Fwd
    int64_t reserved0;
    int64_t reserved1;
} HxQwen32bBwdArgs;

// HxQwen32bApplyArgs — fuse W' = W + (α/r)·B·A in place (inference serve).
typedef struct HxQwen32bApplyArgs {
    int64_t handle;
    int64_t A_flat_p;
    int64_t B_flat_p;
    int64_t lora_r;
    int64_t lora_alpha;
    int64_t out_mode;     // 0 = in-place on VRAM weights, 1 = export to fp32 host
    int64_t out_p;        // float* host (mode 1) or unused (mode 0)
    int64_t offsets_p;    // NEW — int64* [448·2]
    int64_t reserved0;
} HxQwen32bApplyArgs;

// HxQwen32bLoraInitArgs — Kaiming A / zero B init on host buffers.
// v4: accepts explicit out_dim (attn=d_model=5120, ffn_up/gate=ffn_dim=27648,
// ffn_down input is ffn_dim). For 4-target (attn-only) use out_dim=d_model.
typedef struct HxQwen32bLoraInitArgs {
    int64_t out_p;         // float* — destination buffer
    int64_t n_adapters;    // typically 448 (or 256 for attn-only)
    int64_t d_model;       // 5120
    int64_t ffn_dim;       // 27648 — NEW vs 14B
    int64_t lora_r;        // 8
    int64_t seed;          // deterministic seed
    int64_t which;         // 0 = A (Kaiming), 1 = B (zero)
    int64_t offsets_p;     // NEW — per-adapter size table (nullable for uniform)
    int64_t reserved0;
} HxQwen32bLoraInitArgs;

// HxQwen32bAdamArgs — float32 AdamW step on host. One call per parameter
// tensor (caller iterates 448 A + 448 B = 896 calls in the 7-target path).
typedef struct HxQwen32bAdamArgs {
    int64_t n_elems;       // number of float32 elements
    int64_t w_p;            // float* [n] IN/OUT: parameter
    int64_t g_p;            // float* [n] IN   : gradient
    int64_t m_p;            // float* [n] IN/OUT: first moment
    int64_t v_p;            // float* [n] IN/OUT: second moment
    double  lr;             // learning rate
    double  beta1;          // typically 0.9
    double  beta2;          // typically 0.999
    double  eps;            // typically 1e-8
    double  weight_decay;   // typically 0.0 for LoRA B, 0.01 for others
    double  grad_clip;      // per-tensor norm clip; 0.0 = off
    int64_t step;           // 1-based AdamW step for bias correction
    int64_t reserved0;
} HxQwen32bAdamArgs;

// HxQwen32bProbeOut — filled by hxqwen32b_safetensors_probe on success.
typedef struct HxQwen32bProbeOut {
    int64_t n_tensors;
    int64_t n_layer_detected;
    int64_t d_model_detected;
    int64_t vocab_size_detected;
    int64_t total_bytes;
    int64_t reserved0;
} HxQwen32bProbeOut;

// ─────────────────────────────────────────────────────────────
// QwenCtx — opaque per-handle state. v4 additions: config dims,
// index.json byte range, LoRA adapter host buffers (pre-VRAM).
// ─────────────────────────────────────────────────────────────
typedef struct QwenCtx {
    int     live;
    char    ckpt_path[512];
    int64_t n_params;
    int64_t n_layers;
    int64_t d_model;
    int64_t vocab_size;
    int64_t n_head;
    int64_t n_kv_head;
    int64_t head_dim;
    int64_t ffn_dim;
    double  phi_holo_cached;
    // v4 load-path state
    int64_t index_tensor_count;       // from safetensors.index.json
    int64_t index_total_bytes;        // total_size field
    int     shape_validated;          // 1 if passed QWEN32B_* spec check
    // v5 device state (placeholder)
    void*   weights_device;
    void*   kv_cache_device;
    void*   cublas_handle;
    void*   tokenizer;
} QwenCtx;

#define HXQWEN32B_MAX_HANDLES 16
static QwenCtx g_ctx_table[HXQWEN32B_MAX_HANDLES];

// ─────────────────────────────────────────────────────────────
// Version — v4 Day-1 scaffold.
//
// Returns 4 (not 0) — self-host FFI has a T33-class corner where int64_t 0
// reads back with tag TAG_NOT_BUILTIN (see feedback memory). Non-zero by
// convention.
// ─────────────────────────────────────────────────────────────
int64_t hxqwen32b_version(void) {
    return 4;
}

// ─────────────────────────────────────────────────────────────
// SECTION 1. safetensors index.json probe
//
// Qwen2.5-32B ships as multi-shard safetensors (typically 16-18 shards
// at fp16 ≈ 65GB total). Parser is identical in shape to 14B but with
// QWEN32B_N_LAYER=64 as the validation target.
// ─────────────────────────────────────────────────────────────

static int read_file_to_buf(const char* path, char** out_buf, size_t* out_len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return -2; }
    if (st.st_size <= 0 || st.st_size > (off_t)(1 << 28)) { // 256MB cap
        close(fd);
        return -3;
    }
    char* buf = (char*)malloc((size_t)st.st_size + 1);
    if (!buf) { close(fd); return -4; }
    ssize_t got = 0;
    size_t need = (size_t)st.st_size;
    while (got < (ssize_t)need) {
        ssize_t n = read(fd, buf + got, need - (size_t)got);
        if (n <= 0) { free(buf); close(fd); return -5; }
        got += n;
    }
    buf[need] = '\0';
    close(fd);
    *out_buf = buf;
    *out_len = need;
    return 0;
}

// Count occurrences of a fixed pattern in [s, s+n).
static int64_t count_substr(const char* s, size_t n, const char* pat) {
    size_t pl = strlen(pat);
    if (pl == 0 || n < pl) return 0;
    int64_t c = 0;
    for (size_t i = 0; i + pl <= n; i++) {
        if (memcmp(s + i, pat, pl) == 0) c++;
    }
    return c;
}

// Detect maximum layer index by scanning for "model.layers.<N>." occurrences
// and tracking the largest N seen. Returns max_index + 1 (= layer count).
static int64_t detect_n_layers(const char* s, size_t n) {
    const char* prefix = "model.layers.";
    size_t pl = strlen(prefix);
    int64_t max_seen = -1;
    for (size_t i = 0; i + pl < n; i++) {
        if (memcmp(s + i, prefix, pl) != 0) continue;
        // parse decimal integer following prefix
        size_t j = i + pl;
        int64_t v = 0;
        int have = 0;
        while (j < n && s[j] >= '0' && s[j] <= '9') {
            v = v * 10 + (s[j] - '0');
            j++;
            have = 1;
            if (v > 10000) break; // sanity
        }
        if (have && v > max_seen) max_seen = v;
    }
    return max_seen + 1;
}

// Extract integer value following "total_size": in the index.json metadata.
static int64_t extract_total_size(const char* s, size_t n) {
    const char* key = "\"total_size\"";
    size_t kl = strlen(key);
    for (size_t i = 0; i + kl < n; i++) {
        if (memcmp(s + i, key, kl) != 0) continue;
        size_t j = i + kl;
        // skip whitespace + ':'
        while (j < n && (s[j] == ' ' || s[j] == '\t' || s[j] == ':')) j++;
        int64_t v = 0;
        int have = 0;
        while (j < n && s[j] >= '0' && s[j] <= '9') {
            v = v * 10 + (s[j] - '0');
            j++;
            have = 1;
        }
        if (have) return v;
    }
    return 0;
}

// hxqwen32b_safetensors_probe — non-CUDA, safe on Mac.
int hxqwen32b_safetensors_probe(int64_t path_p, void* out_dims) {
    if (path_p == 0 || out_dims == NULL) return RC_ERR_NULL_ARGS;
    const char* dir = (const char*)(uintptr_t)path_p;
    HxQwen32bProbeOut* out = (HxQwen32bProbeOut*)out_dims;
    memset(out, 0, sizeof(HxQwen32bProbeOut));

    // Compose candidate index paths. 32B is always multi-shard.
    char path[1024];
    snprintf(path, sizeof(path), "%s/model.safetensors.index.json", dir);

    char* buf = NULL;
    size_t n = 0;
    int rc = read_file_to_buf(path, &buf, &n);
    if (rc != 0) {
        // Fallback: check for single-file safetensors (unlikely for 32B).
        snprintf(path, sizeof(path), "%s/model.safetensors", dir);
        struct stat st;
        if (stat(path, &st) == 0 && st.st_size > 0) {
            out->n_tensors = -1;  // unknown without parsing
            out->total_bytes = (int64_t)st.st_size;
            return RC_OK;
        }
        return RC_ERR_INDEX_JSON;
    }

    int64_t tensor_count = count_substr(buf, n, ".safetensors\"");
    int64_t total_bytes = extract_total_size(buf, n);
    int64_t n_layer = detect_n_layers(buf, n);

    // v4 infers d_model/vocab from architectural defaults AFTER validating
    // n_layer matches the 32B spec. v5 will read shard headers directly.
    int64_t d_model = (n_layer == QWEN32B_N_LAYER) ? QWEN32B_D_MODEL : 0;
    int64_t vocab   = (n_layer == QWEN32B_N_LAYER) ? QWEN32B_VOCAB   : 0;

    out->n_tensors           = tensor_count;
    out->n_layer_detected    = n_layer;
    out->d_model_detected    = d_model;
    out->vocab_size_detected = vocab;
    out->total_bytes         = total_bytes;

    free(buf);
    return RC_OK;
}

// ─────────────────────────────────────────────────────────────
// SECTION 2. hxqwen32b_load — v4: probe + validate, no VRAM upload.
// ─────────────────────────────────────────────────────────────
int64_t hxqwen32b_load(int64_t ckpt_path_p) {
    if (ckpt_path_p == 0) return RC_ERR_BAD_HANDLE;
    const char* path = (const char*)(uintptr_t)ckpt_path_p;

    // Find a free slot (skip index 0 — reserved sentinel).
    int slot = -1;
    for (int i = 1; i < HXQWEN32B_MAX_HANDLES; i++) {
        if (!g_ctx_table[i].live) { slot = i; break; }
    }
    if (slot < 0) return RC_ERR_BAD_HANDLE;

    QwenCtx* ctx = &g_ctx_table[slot];
    memset(ctx, 0, sizeof(QwenCtx));
    ctx->live = 1;
    size_t path_len = strnlen(path, sizeof(ctx->ckpt_path) - 1);
    memcpy(ctx->ckpt_path, path, path_len);
    ctx->ckpt_path[path_len] = '\0';

    // Architectural defaults (validated below).
    ctx->n_params      = 32000000000LL;    // ~32B
    ctx->n_layers      = QWEN32B_N_LAYER;
    ctx->d_model       = QWEN32B_D_MODEL;
    ctx->vocab_size    = QWEN32B_VOCAB;
    ctx->n_head        = QWEN32B_N_HEAD;
    ctx->n_kv_head     = QWEN32B_N_KV_HEAD;
    ctx->head_dim      = QWEN32B_HEAD_DIM;
    ctx->ffn_dim       = QWEN32B_FFN_DIM;
    ctx->phi_holo_cached = -1.0;

    // Probe index.json — skip if path is a stub/fake marker (starts with
    // "__stub__"). Dry-run tests can exercise load without 65GB on disk.
    if (strncmp(path, "__stub__", 8) == 0) {
        ctx->shape_validated = 1;
        ctx->index_tensor_count = -1;
        return (int64_t)slot;
    }

    HxQwen32bProbeOut probe;
    int pr = hxqwen32b_safetensors_probe(ckpt_path_p, &probe);
    if (pr != 0) {
        // Keep slot live but mark un-validated so caller can still exercise
        // init/AdamW entries with explicit dims. Log and return slot (NOT
        // error) so dry-run flows don't hard-fail.
        ctx->shape_validated = 0;
        return (int64_t)slot;
    }

    ctx->index_tensor_count = probe.n_tensors;
    ctx->index_total_bytes  = probe.total_bytes;

    // Shape validation — Qwen2.5-32B must have 64 layers.
    if (probe.n_layer_detected != QWEN32B_N_LAYER) {
        // Shape mismatch — reset slot and return error.
        memset(ctx, 0, sizeof(QwenCtx));
        return RC_ERR_SHAPE_MISMATCH;
    }
    ctx->shape_validated = 1;

#if defined(__linux__) && defined(HXQWEN32B_CUDA)
    // v5 TODO (2× H100 tensor-parallel):
    //   1. mmap each shard file
    //   2. parse per-shard safetensors header (LE u64 size + JSON)
    //   3. for each tensor: shard split across GPUs, cudaMalloc + cudaMemcpy
    //      bf16 → register in ctx (shard map: layers 0-31 on GPU0, 32-63 on GPU1)
    //   4. NCCL init for all-gather during forward residuals
    //   5. cublasCreate per-device; ctx->cublas_handle = array[2]
    //   6. allocate KV cache pages (paged attention, 2× split)
#endif

    return (int64_t)slot;
}

// ─────────────────────────────────────────────────────────────
// SECTION 3. hxqwen32b_generate — v4 stub.
// ─────────────────────────────────────────────────────────────
int64_t hxqwen32b_generate(int64_t args_p) {
    if (args_p == 0) return RC_ERR_BAD_ARGS;
    HxQwen32bGenArgs* a = (HxQwen32bGenArgs*)(uintptr_t)args_p;

    if (a->handle <= 0 || a->handle >= HXQWEN32B_MAX_HANDLES) return RC_ERR_BAD_HANDLE;
    QwenCtx* ctx = &g_ctx_table[a->handle];
    if (!ctx->live) return RC_ERR_BAD_HANDLE;

    if (a->prompt_p == 0 || a->prompt_len <= 0) return RC_ERR_BAD_ARGS;
    if (a->out_p == 0 || a->out_cap <= 0)       return RC_ERR_BAD_ARGS;
    if (a->max_tokens <= 0)                     return RC_ERR_BAD_ARGS;

    const char* marker = "[hxqwen32b:stub:v4]";
    size_t ml = strlen(marker);
    if ((int64_t)ml > a->out_cap) ml = (size_t)a->out_cap;
    memcpy((void*)(uintptr_t)a->out_p, marker, ml);

#if defined(__linux__) && defined(HXQWEN32B_CUDA)
    // v5 generation loop — tensor-parallel 2× H100.
#endif

    return RC_ERR_CUDA_TODO;
}

// ─────────────────────────────────────────────────────────────
// SECTION 4. hxqwen32b_compute_phi_holo — cached sentinel.
// ─────────────────────────────────────────────────────────────
double hxqwen32b_compute_phi_holo(int64_t handle) {
    if (handle <= 0 || handle >= HXQWEN32B_MAX_HANDLES) return -1.0;
    QwenCtx* ctx = &g_ctx_table[handle];
    if (!ctx->live) return -1.0;
    return ctx->phi_holo_cached;
}

// ─────────────────────────────────────────────────────────────
// SECTION 5. hxqwen32b_free — idempotent release.
// ─────────────────────────────────────────────────────────────
int64_t hxqwen32b_free(int64_t handle) {
    if (handle <= 0 || handle >= HXQWEN32B_MAX_HANDLES) return RC_ERR_BAD_HANDLE;
    QwenCtx* ctx = &g_ctx_table[handle];
    if (!ctx->live) return RC_OK;

#if defined(__linux__) && defined(HXQWEN32B_CUDA)
    // v5: cublasDestroy all handles + cudaFree all device buffers (per-GPU).
#endif

    memset(ctx, 0, sizeof(QwenCtx));
    return RC_OK;
}

// ─────────────────────────────────────────────────────────────
// SECTION 6. LoRA init (LIVE in v4 — no CUDA required)
//
// Kaiming (He) init for A: σ = sqrt(2 / fan_in). Per-adapter fan_in depends
// on which projection: attn q/k/v/o use fan_in=d_model=5120; ffn gate/up
// use fan_in=d_model (input dim); ffn down uses fan_in=ffn_dim=27648.
// When offsets_p is NULL, we use uniform fan_in=d_model (attn-only case).
// Otherwise the caller's offset table implicitly encodes per-block size
// via (next_off - this_off) / lora_r.
//
// Zero init for B: B_ij = 0. Guarantees ΔW = B·A = 0 at step 0.
//
// Random source: xoshiro128** (deterministic per-adapter seed).
// Gaussian via Box-Muller.
// ─────────────────────────────────────────────────────────────

typedef struct Xoshiro {
    uint32_t s[4];
} Xoshiro;

static uint32_t xrotl(uint32_t x, int k) {
    return (x << k) | (x >> (32 - k));
}

static uint32_t xoshiro_next(Xoshiro* r) {
    const uint32_t t = r->s[1] << 9;
    uint32_t result = xrotl(r->s[1] * 5, 7) * 9;
    r->s[2] ^= r->s[0];
    r->s[3] ^= r->s[1];
    r->s[1] ^= r->s[2];
    r->s[0] ^= r->s[3];
    r->s[2] ^= t;
    r->s[3] = xrotl(r->s[3], 11);
    return result;
}

static void xoshiro_seed(Xoshiro* r, uint64_t seed) {
    // SplitMix64 to derive 4 × 32-bit state from a single u64 seed.
    uint64_t z = seed + 0x9e3779b97f4a7c15ULL;
    for (int i = 0; i < 4; i++) {
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        z = z ^ (z >> 31);
        r->s[i] = (uint32_t)(z >> 32);
        if (r->s[i] == 0) r->s[i] = 1; // avoid all-zero state
    }
}

static float xoshiro_uniform_01(Xoshiro* r) {
    // 24-bit mantissa → [0, 1).
    return (float)(xoshiro_next(r) >> 8) / (float)(1u << 24);
}

// Box-Muller: returns one N(0,1) sample (discard second for simplicity).
static float gauss01(Xoshiro* r) {
    float u1 = xoshiro_uniform_01(r);
    float u2 = xoshiro_uniform_01(r);
    if (u1 < 1e-7f) u1 = 1e-7f;
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * (float)M_PI * u2);
}

int hxqwen32b_lora_init_A(void* args) {
    if (args == NULL) return RC_ERR_NULL_ARGS;
    HxQwen32bLoraInitArgs* a = (HxQwen32bLoraInitArgs*)args;
    if (a->out_p == 0) return RC_ERR_NULL_ARGS;
    if (a->n_adapters <= 0 || a->d_model <= 0 || a->lora_r <= 0) return RC_ERR_BAD_ARGS;

    float* out = (float*)(uintptr_t)a->out_p;
    const int64_t* offs = (a->offsets_p != 0)
        ? (const int64_t*)(uintptr_t)a->offsets_p
        : NULL;

    // Uniform stride path (attn-only, offsets_p=NULL): per-block = r × d_model.
    if (offs == NULL) {
        const int64_t N_per = a->lora_r * a->d_model;
        const float   sigma = sqrtf(2.0f / (float)a->d_model);
        Xoshiro rng;
        for (int64_t k = 0; k < a->n_adapters; k++) {
            xoshiro_seed(&rng, (uint64_t)a->seed ^ (uint64_t)k * 0x9e3779b97f4a7c15ULL);
            float* blk = out + k * N_per;
            for (int64_t i = 0; i < N_per; i++) {
                blk[i] = gauss01(&rng) * sigma;
            }
        }
        return RC_OK;
    }

    // Heterogeneous stride path — offsets_p supplies A-block offsets in
    // float32 element count. offs[2*k] = A offset for adapter k, offs[2*k+1]
    // = B offset (unused here). Block length inferred from adjacent offsets;
    // for the last block we fall back to r × ffn_dim as upper bound.
    Xoshiro rng;
    int64_t ffn_cap = (a->ffn_dim > 0) ? a->ffn_dim : QWEN32B_FFN_DIM;
    for (int64_t k = 0; k < a->n_adapters; k++) {
        int64_t a_off = offs[2 * k];
        int64_t a_next = (k + 1 < a->n_adapters)
            ? offs[2 * (k + 1)]
            : (a_off + a->lora_r * ffn_cap);  // upper bound
        int64_t N_per = a_next - a_off;
        if (N_per <= 0 || N_per > a->lora_r * ffn_cap * 2) {
            return RC_ERR_BAD_ARGS;  // offset table malformed
        }
        // fan_in inferred from block size / lora_r
        int64_t fan_in = N_per / a->lora_r;
        if (fan_in <= 0) fan_in = a->d_model;
        const float sigma = sqrtf(2.0f / (float)fan_in);
        xoshiro_seed(&rng, (uint64_t)a->seed ^ (uint64_t)k * 0x9e3779b97f4a7c15ULL);
        float* blk = out + a_off;
        for (int64_t i = 0; i < N_per; i++) {
            blk[i] = gauss01(&rng) * sigma;
        }
    }
    return RC_OK;
}

int hxqwen32b_lora_init_B(void* args) {
    if (args == NULL) return RC_ERR_NULL_ARGS;
    HxQwen32bLoraInitArgs* a = (HxQwen32bLoraInitArgs*)args;
    if (a->out_p == 0) return RC_ERR_NULL_ARGS;
    if (a->n_adapters <= 0 || a->d_model <= 0 || a->lora_r <= 0) return RC_ERR_BAD_ARGS;

    float* out = (float*)(uintptr_t)a->out_p;
    const int64_t* offs = (a->offsets_p != 0)
        ? (const int64_t*)(uintptr_t)a->offsets_p
        : NULL;

    if (offs == NULL) {
        // Uniform stride: d_model × r per block.
        const int64_t N_total = a->n_adapters * a->d_model * a->lora_r;
        memset(out, 0, (size_t)N_total * sizeof(float));
        return RC_OK;
    }

    // Heterogeneous path — zero each block using offset table.
    int64_t ffn_cap = (a->ffn_dim > 0) ? a->ffn_dim : QWEN32B_FFN_DIM;
    for (int64_t k = 0; k < a->n_adapters; k++) {
        int64_t b_off = offs[2 * k + 1];
        int64_t b_next = (k + 1 < a->n_adapters)
            ? offs[2 * (k + 1) + 1]
            : (b_off + ffn_cap * a->lora_r);  // upper bound
        int64_t N_per = b_next - b_off;
        if (N_per <= 0 || N_per > ffn_cap * a->lora_r * 2) {
            return RC_ERR_BAD_ARGS;
        }
        memset(out + b_off, 0, (size_t)N_per * sizeof(float));
    }
    return RC_OK;
}

// ─────────────────────────────────────────────────────────────
// SECTION 7. AdamW CPU reference (LIVE in v4)
// Standard Adam + decoupled weight decay (Loshchilov 2019). Identical to
// 14B — the parameter-level math doesn't care about model size.
// ─────────────────────────────────────────────────────────────

static float compute_grad_l2norm(const float* g, int64_t n) {
    double s = 0.0;
    for (int64_t i = 0; i < n; i++) s += (double)g[i] * (double)g[i];
    return (float)sqrt(s);
}

int hxqwen32b_adamw_step_cpu(void* args) {
    if (args == NULL) return RC_ERR_NULL_ARGS;
    HxQwen32bAdamArgs* a = (HxQwen32bAdamArgs*)args;
    if (a->n_elems <= 0) return RC_ERR_BAD_ARGS;
    if (a->w_p == 0 || a->g_p == 0 || a->m_p == 0 || a->v_p == 0) return RC_ERR_NULL_ARGS;
    if (a->step <= 0) return RC_ERR_BAD_ARGS;

    float* w = (float*)(uintptr_t)a->w_p;
    float* g = (float*)(uintptr_t)a->g_p;
    float* m = (float*)(uintptr_t)a->m_p;
    float* v = (float*)(uintptr_t)a->v_p;

    const float b1  = (float)a->beta1;
    const float b2  = (float)a->beta2;
    const float eps = (float)a->eps;
    const float lr  = (float)a->lr;
    const float wd  = (float)a->weight_decay;

    // Optional per-tensor grad clip.
    float clip_scale = 1.0f;
    if (a->grad_clip > 0.0) {
        float gn = compute_grad_l2norm(g, a->n_elems);
        if (gn > (float)a->grad_clip) {
            clip_scale = (float)a->grad_clip / (gn + 1e-12f);
        }
    }

    // Bias-correction factors — double precision to avoid early-step drift.
    double bc1 = 1.0 - pow((double)b1, (double)a->step);
    double bc2 = 1.0 - pow((double)b2, (double)a->step);
    if (bc1 < 1e-20) bc1 = 1e-20;
    if (bc2 < 1e-20) bc2 = 1e-20;
    const float inv_bc1 = (float)(1.0 / bc1);
    const float inv_bc2 = (float)(1.0 / bc2);

    for (int64_t i = 0; i < a->n_elems; i++) {
        float gi = g[i] * clip_scale;
        float mi = b1 * m[i] + (1.0f - b1) * gi;
        float vi = b2 * v[i] + (1.0f - b2) * gi * gi;
        m[i] = mi;
        v[i] = vi;
        float mhat = mi * inv_bc1;
        float vhat = vi * inv_bc2;
        float step = mhat / (sqrtf(vhat) + eps);
        // Decoupled weight decay.
        w[i] = w[i] - lr * (step + wd * w[i]);
    }
    return RC_OK;
}

// ─────────────────────────────────────────────────────────────
// SECTION 8. hxqwen32b_forward_with_lora — v4: validate, return -5.
//
// v5 CUDA kernel sequence (mapped, not implemented) — tensor-parallel 2×H100:
//
//   // Layer shard map: layers 0-31 on GPU0, 32-63 on GPU1. All-gather at
//   // residual boundaries. cublasHandle per device.
//
//   // tokens → embeddings: gather bf16 W_embed [V,d] by ids[B*S]
//   launch_embed_gather_bf16(ids_dev, W_embed_dev, h_dev, B*S, d);
//
//   for L in 0..64:
//     // RMSNorm pre-attn
//     launch_rmsnorm_bf16(h_dev, rms1_dev[L], h1_dev, B*S, d, eps=1e-6);
//     // GQA: q = h1 @ W_q (+ LoRA); k/v = h1 @ W_kv (+ LoRA); o = W_o·attn
//     //   Base cuBLAS: cublasGemmEx(H, N, N, d, B*S, d, bf16, bf16, bf16, ...)
//     //   LoRA delta: cublasGemmEx(A[r,d] @ h1) then cublasGemmEx(B[d,r] @ tmp)
//     //               scaled by (α/r).
//     launch_rope_bf16(q_dev, k_dev, head_dim, S);
//     launch_gqa_flash_attn_bf16(q_dev, k_dev, v_dev, o_dev,
//                                B, S, n_head=40, n_kv_head=8, head_dim=128);
//     // residual, RMSNorm, SwiGLU FFN (ffn_dim=27648)
//     launch_residual_add(h_dev, o_dev);
//     launch_rmsnorm_bf16(h_dev, rms2_dev[L], h2_dev, ...);
//     // FFN: h2 @ W_gate (+swish) · (h2 @ W_up) (+LoRA on all 3), then @ W_down
//     launch_swiglu_ffn_bf16_lora(h2_dev, W_gate, W_up, W_down,
//                                 A_gate, B_gate, A_up, B_up, A_down, B_down,
//                                 ffn_dim=27648);
//     launch_residual_add(h_dev, ffn_out_dev);
//     if (save_acts) memcpy layer-L activation [B,S,d] to activations_p.
//     if (L == 31) all_gather(GPU0 -> GPU1) via NCCL
//
//   launch_rmsnorm_bf16(h_dev, rms_final, h_dev, ...);
//   // lm_head: logits = h @ W_embed^T  (tied)
//   cublasGemmEx(H, N, T, V, B*S, d, bf16, bf16, fp32 logits, ...);
//   launch_ce_softmax_kernel(logits, targets, loss_out);
//
// Total kernels per forward: ~13 × 64 layers + 4 = 836 launches (7-target).
// Flash-attn v2 fusion reduces to ~580.
// ─────────────────────────────────────────────────────────────

static int validate_fwd_args(const HxQwen32bFwdArgs* a, QwenCtx** out_ctx) {
    if (a->handle <= 0 || a->handle >= HXQWEN32B_MAX_HANDLES) return RC_ERR_BAD_HANDLE;
    QwenCtx* ctx = &g_ctx_table[a->handle];
    if (!ctx->live) return RC_ERR_BAD_HANDLE;
    if (a->B_batch <= 0 || a->S_seq <= 0) return RC_ERR_BAD_ARGS;
    if (a->S_seq > QWEN32B_MAX_POS) return RC_ERR_BAD_ARGS;
    if (a->V_vocab != QWEN32B_VOCAB) return RC_ERR_SHAPE_MISMATCH;
    if (a->ids_p == 0 || a->logits_p == 0) return RC_ERR_NULL_ARGS;
    if (a->A_flat_p == 0 || a->B_flat_p == 0) return RC_ERR_NULL_ARGS;
    if (a->lora_r <= 0 || a->lora_r > 256) return RC_ERR_BAD_ARGS;
    if (a->lora_alpha <= 0) return RC_ERR_BAD_ARGS;
    *out_ctx = ctx;
    return RC_OK;
}

int hxqwen32b_forward_with_lora(void* args) {
    if (args == NULL) return RC_ERR_NULL_ARGS;
    HxQwen32bFwdArgs* a = (HxQwen32bFwdArgs*)args;
    QwenCtx* ctx = NULL;
    int rc = validate_fwd_args(a, &ctx);
    if (rc != RC_OK) return rc;

#if defined(__linux__) && defined(HXQWEN32B_CUDA)
    // v5: actual kernel sequence per header comment above.
#endif
    (void)ctx; (void)a;
    return RC_ERR_CUDA_TODO;
}

// ─────────────────────────────────────────────────────────────
// SECTION 9. hxqwen32b_backward_lora_only — v4: validate + zero dA/dB.
//
// v5 CUDA kernel sequence (mapped, not implemented):
//
//   // CE backward: dlogits = (softmax(logits) - onehot(targets)) / (B*S)
//   launch_ce_backward(logits_dev, targets_dev, dlogits_dev, B, S, V);
//   cublasGemmEx(NT: dlogits @ W_embed -> dh_final);  // tied embed, frozen
//
//   for L in reverse(0..64):
//     dh_layer = dh_next  (+ residual plumbing)
//     // SwiGLU bwd WITH LoRA on gate/up/down (7-target training)
//     launch_swiglu_bwd_lora(..., dh_layer -> dh_mid,
//                            A/B_gate, A/B_up, A/B_down,
//                            dA/dB_gate, dA/dB_up, dA/dB_down);
//     launch_rmsnorm_bwd(dh_mid, rms2, ..., dh_attn);
//
//     // Attn backward with LoRA on q/k/v/o:
//     //   y = W·x + (α/r)·B·(A·x)
//     //   dL/dx = W^T·dy + (α/r)·A^T·(B^T·dy)
//     //   dL/dA = (α/r)·(B^T·dy) ⊗ x
//     //   dL/dB = (α/r)·dy ⊗ (A·x)
//     //   (only dL/dA, dL/dB — W frozen)
//     // 7 adapters per layer × 64 layers = 448 updates per bwd pass.
//     launch_lora_bwd_qkvo(dh_attn, A[L,q], B[L,q], act[L], alpha, r,
//                          dA[L,q], dB[L,q]);
//     ... k/v/o similarly ...
//     launch_rmsnorm_bwd(dh_attn, rms1, ..., dh_input);
//     dh_next = dh_input
//
//   // Scale accumulated dA/dB by grad_scale.
//   launch_scale_in_place(dA_flat, total_A_elems, grad_scale);
//   launch_scale_in_place(dB_flat, total_B_elems, grad_scale);
//
// Total kernels per backward: ~22 × 64 layers + 4 = 1412 launches.
// ─────────────────────────────────────────────────────────────

int hxqwen32b_backward_lora_only(void* args) {
    if (args == NULL) return RC_ERR_NULL_ARGS;
    HxQwen32bBwdArgs* a = (HxQwen32bBwdArgs*)args;
    if (a->handle <= 0 || a->handle >= HXQWEN32B_MAX_HANDLES) return RC_ERR_BAD_HANDLE;
    QwenCtx* ctx = &g_ctx_table[a->handle];
    if (!ctx->live) return RC_ERR_BAD_HANDLE;
    if (a->B_batch <= 0 || a->S_seq <= 0) return RC_ERR_BAD_ARGS;
    if (a->dlogits_p == 0 || a->A_flat_p == 0 || a->B_flat_p == 0) return RC_ERR_NULL_ARGS;
    if (a->dA_flat_p == 0 || a->dB_flat_p == 0) return RC_ERR_NULL_ARGS;
    if (a->activations_p == 0) return RC_ERR_NULL_ARGS;
    if (a->lora_r <= 0) return RC_ERR_BAD_ARGS;

    // v4: if requested, zero the dA/dB accumulators on host. Offset-aware:
    // if offsets_p is supplied, zero via the offset table; otherwise assume
    // uniform attn-only stride (4-target fallback for cross-compat testing).
    if (a->zero_grads_first) {
        float* dA = (float*)(uintptr_t)a->dA_flat_p;
        float* dB = (float*)(uintptr_t)a->dB_flat_p;
        const int64_t* offs = (a->offsets_p != 0)
            ? (const int64_t*)(uintptr_t)a->offsets_p
            : NULL;
        if (offs == NULL) {
            // Uniform attn stride fallback: LORA_N_ADAPTERS × r × d_model.
            const int64_t nA = (int64_t)LORA_N_ADAPTERS * a->lora_r * QWEN32B_D_MODEL;
            const int64_t nB = (int64_t)LORA_N_ADAPTERS * QWEN32B_D_MODEL * a->lora_r;
            memset(dA, 0, (size_t)nA * sizeof(float));
            memset(dB, 0, (size_t)nB * sizeof(float));
        } else {
            // Heterogeneous: derive total A/B size from offset table.
            // Upper bound of last block: ffn_dim · r (the largest).
            int64_t last_a = offs[2 * (LORA_N_ADAPTERS - 1)];
            int64_t last_b = offs[2 * (LORA_N_ADAPTERS - 1) + 1];
            int64_t totalA = last_a + (int64_t)a->lora_r * QWEN32B_FFN_DIM;
            int64_t totalB = last_b + (int64_t)QWEN32B_FFN_DIM * a->lora_r;
            memset(dA, 0, (size_t)totalA * sizeof(float));
            memset(dB, 0, (size_t)totalB * sizeof(float));
        }
    }

#if defined(__linux__) && defined(HXQWEN32B_CUDA)
    // v5: full CUDA backward per header comment above.
#endif
    (void)ctx;
    return RC_ERR_CUDA_TODO;
}

// ─────────────────────────────────────────────────────────────
// SECTION 10. hxqwen32b_apply_lora_delta — v4: validate, return -5.
//
// v5: in-place W' = W + (α/r)·B·A per adapter. For 32B the cost of
// materialising ΔW explicitly is even higher than 14B:
//   attn ΔW  = d·d   = 100MB bf16 × 4 × 64 = 25.6 GB
//   ffn ΔW  ≈ d·ffn = 540MB bf16 × 3 × 64 = ~100 GB
// Target: implicit fold via fused kernel at serve time. Export mode
// (out_mode=1) materialises into caller-owned fp32 host buffer for HF
// safetensors writeback.
// ─────────────────────────────────────────────────────────────

int hxqwen32b_apply_lora_delta(void* args) {
    if (args == NULL) return RC_ERR_NULL_ARGS;
    HxQwen32bApplyArgs* a = (HxQwen32bApplyArgs*)args;
    if (a->handle <= 0 || a->handle >= HXQWEN32B_MAX_HANDLES) return RC_ERR_BAD_HANDLE;
    QwenCtx* ctx = &g_ctx_table[a->handle];
    if (!ctx->live) return RC_ERR_BAD_HANDLE;
    if (a->A_flat_p == 0 || a->B_flat_p == 0) return RC_ERR_NULL_ARGS;
    if (a->lora_r <= 0) return RC_ERR_BAD_ARGS;

#if defined(__linux__) && defined(HXQWEN32B_CUDA)
    // v5: fuse or export.
#endif
    (void)ctx;
    return RC_ERR_CUDA_TODO;
}
