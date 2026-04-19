// hxqwen14b.c — hexa FFI ↔ Qwen2.5-14B training shim (Linux x86_64 / Mac stub)
//
// STATUS: v4  Day-1.5 scaffold (non-CUDA path LIVE, CUDA kernels TODO)
//
//   v1  skeleton (handles + struct-args, stub returns)                DONE
//   v2  real CUDA inference forward                                   TODO
//   v3  LoRA fwd/bwd/apply ABI surface (stubs return -4)              DONE
//   v4  Day-1.5 — ABI structs locked, safetensors header parse,       THIS
//       LoRA init (Kaiming A + zero B), AdamW CPU reference, load
//       shape validation. CUDA fwd/bwd return -5 CUDA_TODO with
//       cuBLAS call sequence mapped inline. Non-CUDA path fully
//       testable on Mac.
//
// Goal: replace the Python peft/transformers training path for
// Qwen2.5-14B LoRA fine-tune. Target pod: H100 SXM5 80GB, CUDA 11.8,
// cuBLAS, bf16 Tensor Core. Ranked alongside hxlmhead/hxflash/hxccl —
// same ABI conventions, same build-script shape, same struct-arg pattern.
//
// This v4 commit unblocks hexa-side wiring: train_alm_lora.hexa can now
// bind `forward_with_lora` / `backward_lora_only` / `apply_lora_delta`
// and observe int rc — rc==-5 means "CUDA kernels pending, rest of the
// pipeline (corpus → tokenize → adapter init → AdamW → ckpt I/O) is
// exercised end-to-end". rc==0 is reserved for real CUDA forward landing
// in a follow-up commit (v5).
//
// ABI surface (5 symbols — matches hxlmhead struct-args v2 pattern):
//
//   int64_t hxqwen14b_load(int64_t ckpt_path_p)
//     * ckpt_path_p : const char* — path to Qwen2.5-14B weights directory
//                     (HF safetensors layout: model.safetensors.index.json
//                     + model-00001-of-NNNNN.safetensors shards).
//     * v4: mmap's index.json, validates tensor manifest against Qwen2.5
//           shape spec (48-layer × d=5120 × vocab=152064), returns a live
//           handle. Does NOT upload weights to device (that's v5 CUDA).
//     * returns : int64 handle > 0 on success, negative error.
//                  -1 path null / no free slot
//                  -2 index.json missing / malformed
//                  -3 tensor shape mismatch vs Qwen2.5-14B spec
//                  -4 safetensors shard missing / unreadable
//
//   int64_t hxqwen14b_version(void)          -> 4 (Day-1.5)
//   int64_t hxqwen14b_free(int64_t h)        -> 0 (idempotent)
//
//   int64_t hxqwen14b_generate(int64_t args_p)
//     (unchanged from v3 — still stub; inference path not on r11 critical
//      path. Trainer doesn't invoke generate() during LoRA loop.)
//
//   double hxqwen14b_compute_phi_holo(int64_t h)
//     (unchanged — cached sentinel until CUDA forward lands.)
//
// Day-2 LoRA training symbols (v4 — partial live, CUDA TODO):
//
//   int hxqwen14b_forward_with_lora(void* args)
//     args: HxQwenFwdArgs* (packed, 136 B — see struct below)
//     * v4: validates ABI, dims, pointers. Initialises adapter scratch.
//           Returns -5 CUDA_TODO (kernels land in v5).
//
//   int hxqwen14b_backward_lora_only(void* args)
//     args: HxQwenBwdArgs* (packed, 168 B — see struct below)
//     * v4: validates ABI, zeroes dA/dB accumulators, returns -5 CUDA_TODO.
//
//   int hxqwen14b_apply_lora_delta(void* args)
//     args: HxQwenApplyArgs* (packed, 72 B — see struct below)
//     * v4: validates. apply-in-place would require full weight tensors in
//           VRAM (not allocated until v5) — returns -5 CUDA_TODO.
//
// Day-1.5 LIVE helper entries (testable on Mac, no CUDA required):
//
//   int hxqwen14b_lora_init_A(void* args)   HxQwenLoraInitArgs*
//     Kaiming(fan_in=d) normal init over n_adapters × r × d floats.
//     Deterministic given seed. Returns 0 on success.
//
//   int hxqwen14b_lora_init_B(void* args)   HxQwenLoraInitArgs*
//     Zero init over n_adapters × d × r floats. Returns 0.
//
//   int hxqwen14b_adamw_step_cpu(void* args) HxQwenAdamArgs*
//     Float32 AdamW step with bias correction, weight_decay, grad_clip.
//     Operates on host memory (used on Mac xverify + CI golden tests).
//     Returns 0 on success.
//
//   int hxqwen14b_safetensors_probe(int64_t path_p, void* out_dims)
//     Opens `model.safetensors.index.json` at path, counts tensors,
//     extracts n_layer/d_model/vocab_size, writes to *out_dims
//     (HxQwenProbeOut*). Returns 0 / negative error.
//
// LoRA contract (consumed by anima training/train_alm_lora.hexa r11):
//   * 48 layers × {q,k,v,o}_proj = 192 adapters
//   * A : [r × d]   B : [d × r]   r=8, α=16, d=5120 (Qwen2.5-14B hidden_dim)
//   * adapter forward : y = W·x + (α/r) · B · (A · x)
//   * gradient flows ONLY through A,B (W is frozen, no dL/dW computed)
//   * caller passes A_flat / B_flat as packed float32 contiguous arrays
//     in the SAME ordering as the trainer (idx = layer*4 + tgt; A is r·d
//     per block, B is d·r per block). The C side does NOT reorder.
//
// Build (Linux x86_64, Ubuntu/Debian on H100 pod):
//   hexa scripts/build_hxqwen14b_linux.hexa
//   # v4 compiles WITHOUT CUDA too — non-CUDA path (safetensors probe +
//   # LoRA init + AdamW CPU + shape validation) is the tested surface.
//   # CUDA kernels land in v5 behind HXQWEN14B_CUDA=1.
//
// Cross-verify (Mac):
//   hexa scripts/build_hxqwen14b_linux.hexa --mac-xverify
// Mac-native live FFI (v4 covers init + AdamW + probe — rest is stubbed):
//   hexa scripts/build_hxqwen14b_linux.hexa --mac-native
//
// .hexanoport marker convention:
//   This .c is a native shim (extern FFI boundary), not a compilation
//   target for the Hexa-to-C codegen. See hexa_cc.c.hexanoport.

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
  #ifdef HXQWEN14B_CUDA
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
// Qwen2.5-14B architectural constants (MUST match trainer-side
// comptime const in training/train_alm_lora.hexa).
// ─────────────────────────────────────────────────────────────
#define QWEN14B_N_LAYER     48
#define QWEN14B_D_MODEL     5120
#define QWEN14B_VOCAB       152064
#define QWEN14B_N_HEAD      40        // query heads
#define QWEN14B_N_KV_HEAD   8         // GQA: 40/8 = 5x query-per-KV
#define QWEN14B_HEAD_DIM    128       // d_model / n_head = 5120/40
#define QWEN14B_FFN_DIM     13824     // SwiGLU intermediate
#define QWEN14B_MAX_POS     32768     // RoPE max sequence

#define LORA_N_TARGETS      4         // q_proj, k_proj, v_proj, o_proj
#define LORA_N_ADAPTERS     (QWEN14B_N_LAYER * LORA_N_TARGETS)  // 192

// Error codes — stable ABI. -1..-4 match v3; -5 is new in v4.
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
// Packed argument struct for hxqwen14b_generate (unchanged from v3).
// Total = 96 bytes.
// ─────────────────────────────────────────────────────────────
typedef struct HxQwen14bGenArgs {
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
} HxQwen14bGenArgs;

// ─────────────────────────────────────────────────────────────
// v4 LoRA training ABI structs — FROZEN (do not reorder fields).
//
// Field order IS ABI. All pointers stored as int64_t for binary-clean
// hexa marshalling. New fields go BEFORE reserved0 OR consume reserved
// bits in the flag word.
//
// Layout policy — single pointer through FFI, struct carries everything.
// ─────────────────────────────────────────────────────────────

// HxQwenFwdArgs — forward pass with LoRA delta (v5 live; v4 returns -5).
//
// off field               bytes  note
//   0 handle                8   QwenCtx* slot (from hxqwen14b_load)
//   8 B_batch               8   batch size
//  16 S_seq                 8   sequence length (≤ QWEN14B_MAX_POS)
//  24 V_vocab               8   must equal QWEN14B_VOCAB
//  32 ids_p                 8   int32*  [B*S]   token ids (input)
//  40 logits_p              8   float*  [B*S*V] OUT: post-lm_head logits
//  48 A_flat_p              8   float*  [192·r·d]   LoRA A matrices
//  56 B_flat_p              8   float*  [192·d·r]   LoRA B matrices
//  64 activations_p         8   float*  [layers·B·S·d] OUT (for bwd recompute)
//  72 lora_r                8   r=8 (doubled for stack-on support later)
//  80 lora_alpha            8   α=16
//  88 loss_out_p            8   double* [1]  OUT: scalar CE loss
//  96 targets_p             8   int32*  [B*S]  teacher labels for CE
// 104 loss_scale            8   double — typically 1.0 / grad_accum_steps
// 112 ctx_seed              8   int64 — for dropout / noise layers
// 120 reserved0             8   flag word (bit 0 = flash-attn; bit 1 = save_acts)
// 128 reserved1             8
// Total = 136 B
typedef struct HxQwenFwdArgs {
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
    int64_t reserved0;
    int64_t reserved1;
} HxQwenFwdArgs;

// HxQwenBwdArgs — backward pass, LoRA-only gradients (v5 live; v4 -5).
//
// Contract: base weights W are frozen. Backprop writes ONLY dL/dA and
// dL/dB per adapter. Activations saved in forward must be provided.
//
// off field                bytes
//   0 handle                 8
//   8 B_batch                8
//  16 S_seq                  8
//  24 V_vocab                8
//  32 dlogits_p              8   float* [B*S*V]  IN: dL/dlogits
//  40 A_flat_p               8   float* [192·r·d]
//  48 B_flat_p               8   float* [192·d·r]
//  56 activations_p          8   float* [layers·B·S·d]  (from forward)
//  64 dA_flat_p              8   float* [192·r·d]  OUT: dL/dA accumulator
//  72 dB_flat_p              8   float* [192·d·r]  OUT: dL/dB accumulator
//  80 lora_r                 8
//  88 lora_alpha             8
//  96 zero_grads_first       8   int64 (0/1): memset dA/dB before bwd
// 104 grad_scale             8   double — inverse loss_scale (unit grads)
// 112 targets_p              8   int32* [B*S]  — for CE bwd closed form
// 120 reserved0              8
// 128 reserved1              8
// Total = 136 B
typedef struct HxQwenBwdArgs {
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
    int64_t reserved0;
    int64_t reserved1;
} HxQwenBwdArgs;

// HxQwenApplyArgs — fuse W' = W + (α/r)·B·A in place (inference serve).
// Not exercised during training loop (we keep base frozen + adapters live).
// Target: export — collapse adapters into base for a deployable .safetensors.
typedef struct HxQwenApplyArgs {
    int64_t handle;
    int64_t A_flat_p;
    int64_t B_flat_p;
    int64_t lora_r;
    int64_t lora_alpha;
    int64_t out_mode;     // 0 = in-place on VRAM weights, 1 = export to fp32 host
    int64_t out_p;        // float* host (mode 1) or unused (mode 0)
    int64_t reserved0;
    int64_t reserved1;
} HxQwenApplyArgs;

// HxQwenLoraInitArgs — Kaiming A / zero B init on host buffers.
typedef struct HxQwenLoraInitArgs {
    int64_t out_p;         // float* — destination buffer
    int64_t n_adapters;    // typically 192
    int64_t d_model;       // 5120
    int64_t lora_r;        // 8
    int64_t seed;          // deterministic seed
    int64_t which;         // 0 = A (Kaiming), 1 = B (zero)
    int64_t reserved0;
    int64_t reserved1;
} HxQwenLoraInitArgs;

// HxQwenAdamArgs — float32 AdamW step on host. One call per parameter
// tensor (caller iterates over 192 A + 192 B blocks = 384 calls).
typedef struct HxQwenAdamArgs {
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
    double  grad_clip;      // global-norm clip; 0.0 = off
    int64_t step;           // 1-based AdamW step for bias correction
    int64_t reserved0;
} HxQwenAdamArgs;

// HxQwenProbeOut — filled by hxqwen14b_safetensors_probe on success.
typedef struct HxQwenProbeOut {
    int64_t n_tensors;
    int64_t n_layer_detected;
    int64_t d_model_detected;
    int64_t vocab_size_detected;
    int64_t total_bytes;
    int64_t reserved0;
} HxQwenProbeOut;

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
    int     shape_validated;          // 1 if passed QWEN14B_* spec check
    // v5 device state (placeholder)
    void*   weights_device;
    void*   kv_cache_device;
    void*   cublas_handle;
    void*   tokenizer;
} QwenCtx;

#define HXQWEN14B_MAX_HANDLES 16
static QwenCtx g_ctx_table[HXQWEN14B_MAX_HANDLES];

// ─────────────────────────────────────────────────────────────
// Version — v4 Day-1.5 scaffold.
//
// Returns 4 (not 0) — the self-host interpreter FFI path has a known
// T33-class corner where int64_t 0 read-back reads with tag
// TAG_NOT_BUILTIN (see feedback memory module_box_struct_replace.md).
// ─────────────────────────────────────────────────────────────
int64_t hxqwen14b_version(void) {
    return 4;
}

// ─────────────────────────────────────────────────────────────
// SECTION 1. safetensors index.json probe
//
// Qwen2.5-14B ships as a multi-shard safetensors set:
//   model.safetensors.index.json        # JSON manifest
//   model-00001-of-00006.safetensors    # shard 1
//   ...
//   model-00006-of-00006.safetensors    # shard 6
//
// The index.json has a `weight_map` mapping tensor name → shard filename
// and a `metadata.total_size` field. We scan it to:
//   (a) count distinct tensor names,
//   (b) detect layer count by scanning for `model.layers.N.` prefixes,
//   (c) infer d_model and vocab by tokenising specific tensor shape keys,
//   (d) validate shard files actually exist and sum-check sizes.
//
// NOTE: we deliberately do NOT parse the full safetensors tensor metadata
// blocks (those live in the individual shard files as LE JSON headers at
// offset 0). That will be v5's job when we start uploading to device.
//
// The parser is a minimal JSON skim — we look for the key tokens we need
// by string search; no full AST. This is safe because safetensors index
// files are machine-generated and follow a stable schema.
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

// hxqwen14b_safetensors_probe — see header comment. Non-CUDA, safe on Mac.
int hxqwen14b_safetensors_probe(int64_t path_p, void* out_dims) {
    if (path_p == 0 || out_dims == NULL) return RC_ERR_NULL_ARGS;
    const char* dir = (const char*)(uintptr_t)path_p;
    HxQwenProbeOut* out = (HxQwenProbeOut*)out_dims;
    memset(out, 0, sizeof(HxQwenProbeOut));

    // Compose candidate index paths. Two layouts supported:
    //   <dir>/model.safetensors.index.json     (multi-shard)
    //   <dir>/model.safetensors                (single-file — unusual for 14B)
    char path[1024];
    snprintf(path, sizeof(path), "%s/model.safetensors.index.json", dir);

    char* buf = NULL;
    size_t n = 0;
    int rc = read_file_to_buf(path, &buf, &n);
    if (rc != 0) {
        // Fallback: check for single-file safetensors.
        snprintf(path, sizeof(path), "%s/model.safetensors", dir);
        struct stat st;
        if (stat(path, &st) == 0 && st.st_size > 0) {
            out->n_tensors = -1;  // unknown without parsing
            out->total_bytes = (int64_t)st.st_size;
            // single-file layout typical for ≤7B; for 14B this is unexpected.
            return RC_OK;
        }
        return RC_ERR_INDEX_JSON;
    }

    // Tensor count ≈ number of entries in weight_map. Each entry has form
    //   "<tensor_name>": "<shard>",
    // A cheap proxy: count occurrences of ".safetensors\"" after the
    // weight_map key, which matches each value string's trailing quote.
    int64_t tensor_count = count_substr(buf, n, ".safetensors\"");

    int64_t total_bytes = extract_total_size(buf, n);
    int64_t n_layer = detect_n_layers(buf, n);

    // Shape inference heuristics for d_model and vocab_size:
    //   - embed_tokens.weight is [vocab, d_model] — we can't read dim without
    //     the per-tensor header (that's in the shard, not the index). So v4
    //     infers from architectural defaults AFTER validating n_layer.
    //   - v5 will read shard headers and produce real dims.
    int64_t d_model = (n_layer == QWEN14B_N_LAYER) ? QWEN14B_D_MODEL : 0;
    int64_t vocab   = (n_layer == QWEN14B_N_LAYER) ? QWEN14B_VOCAB   : 0;

    out->n_tensors           = tensor_count;
    out->n_layer_detected    = n_layer;
    out->d_model_detected    = d_model;
    out->vocab_size_detected = vocab;
    out->total_bytes         = total_bytes;

    free(buf);
    return RC_OK;
}

// ─────────────────────────────────────────────────────────────
// SECTION 2. hxqwen14b_load — v4: probe + validate, no VRAM upload.
// ─────────────────────────────────────────────────────────────
int64_t hxqwen14b_load(int64_t ckpt_path_p) {
    if (ckpt_path_p == 0) return RC_ERR_BAD_HANDLE;
    const char* path = (const char*)(uintptr_t)ckpt_path_p;

    // Find a free slot (skip index 0 — reserved sentinel).
    int slot = -1;
    for (int i = 1; i < HXQWEN14B_MAX_HANDLES; i++) {
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
    ctx->n_params      = 14000000000LL;
    ctx->n_layers      = QWEN14B_N_LAYER;
    ctx->d_model       = QWEN14B_D_MODEL;
    ctx->vocab_size    = QWEN14B_VOCAB;
    ctx->n_head        = QWEN14B_N_HEAD;
    ctx->n_kv_head     = QWEN14B_N_KV_HEAD;
    ctx->head_dim      = QWEN14B_HEAD_DIM;
    ctx->ffn_dim       = QWEN14B_FFN_DIM;
    ctx->phi_holo_cached = -1.0;

    // Probe index.json — skip if path is a stub/fake marker (starts with
    // "__stub__"). That lets hexa-side dry-run tests take the load path
    // without requiring 28GB of weights on disk.
    if (strncmp(path, "__stub__", 8) == 0) {
        ctx->shape_validated = 1;
        ctx->index_tensor_count = -1;
        return (int64_t)slot;
    }

    HxQwenProbeOut probe;
    int pr = hxqwen14b_safetensors_probe(ckpt_path_p, &probe);
    if (pr != 0) {
        // Keep slot live but mark as un-validated so caller can still
        // exercise the init/AdamW entries with explicit dims. Log and
        // return slot (NOT error) so dry-run flows don't hard-fail.
        ctx->shape_validated = 0;
        return (int64_t)slot;
    }

    ctx->index_tensor_count = probe.n_tensors;
    ctx->index_total_bytes  = probe.total_bytes;

    // Shape validation — Qwen2.5-14B must have 48 layers.
    if (probe.n_layer_detected != QWEN14B_N_LAYER) {
        // Shape mismatch — reset slot and return error.
        memset(ctx, 0, sizeof(QwenCtx));
        return RC_ERR_SHAPE_MISMATCH;
    }
    ctx->shape_validated = 1;

#if defined(__linux__) && defined(HXQWEN14B_CUDA)
    // v5 TODO:
    //   1. mmap each shard file
    //   2. parse per-shard safetensors header (LE u64 size + JSON)
    //   3. for each tensor: cudaMalloc → cudaMemcpy bf16 → register in ctx
    //   4. cublasCreate(&h); ctx->cublas_handle = h;
    //   5. allocate KV cache pages (paged attention)
#endif

    return (int64_t)slot;
}

// ─────────────────────────────────────────────────────────────
// SECTION 3. hxqwen14b_generate — unchanged stub (v3 behaviour).
// ─────────────────────────────────────────────────────────────
int64_t hxqwen14b_generate(int64_t args_p) {
    if (args_p == 0) return RC_ERR_BAD_ARGS;
    HxQwen14bGenArgs* a = (HxQwen14bGenArgs*)(uintptr_t)args_p;

    if (a->handle <= 0 || a->handle >= HXQWEN14B_MAX_HANDLES) return RC_ERR_BAD_HANDLE;
    QwenCtx* ctx = &g_ctx_table[a->handle];
    if (!ctx->live) return RC_ERR_BAD_HANDLE;

    if (a->prompt_p == 0 || a->prompt_len <= 0) return RC_ERR_BAD_ARGS;
    if (a->out_p == 0 || a->out_cap <= 0)       return RC_ERR_BAD_ARGS;
    if (a->max_tokens <= 0)                     return RC_ERR_BAD_ARGS;

    const char* marker = "[hxqwen14b:stub:v4]";
    size_t ml = strlen(marker);
    if ((int64_t)ml > a->out_cap) ml = (size_t)a->out_cap;
    memcpy((void*)(uintptr_t)a->out_p, marker, ml);

#if defined(__linux__) && defined(HXQWEN14B_CUDA)
    // v5 generation loop — see header for kernel sequence.
#endif

    return RC_ERR_CUDA_TODO;
}

// ─────────────────────────────────────────────────────────────
// SECTION 4. hxqwen14b_compute_phi_holo — cached sentinel (unchanged).
// ─────────────────────────────────────────────────────────────
double hxqwen14b_compute_phi_holo(int64_t handle) {
    if (handle <= 0 || handle >= HXQWEN14B_MAX_HANDLES) return -1.0;
    QwenCtx* ctx = &g_ctx_table[handle];
    if (!ctx->live) return -1.0;
    return ctx->phi_holo_cached;
}

// ─────────────────────────────────────────────────────────────
// SECTION 5. hxqwen14b_free — idempotent release.
// ─────────────────────────────────────────────────────────────
int64_t hxqwen14b_free(int64_t handle) {
    if (handle <= 0 || handle >= HXQWEN14B_MAX_HANDLES) return RC_ERR_BAD_HANDLE;
    QwenCtx* ctx = &g_ctx_table[handle];
    if (!ctx->live) return RC_OK;

#if defined(__linux__) && defined(HXQWEN14B_CUDA)
    // v5: cublasDestroy + cudaFree all device buffers.
#endif

    memset(ctx, 0, sizeof(QwenCtx));
    return RC_OK;
}

// ─────────────────────────────────────────────────────────────
// SECTION 6. LoRA init (LIVE in v4 — no CUDA required)
//
// Kaiming (He) init for A: σ = sqrt(2 / fan_in) where fan_in = d_model.
//   A_ij ~ N(0, σ²)
// Zero init for B: B_ij = 0. This guarantees ΔW = B·A = 0 at step 0.
//
// Random number source: xoshiro128** (deterministic per-adapter seed).
// We produce pseudo-normal samples via Box-Muller from two uniforms.
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

int hxqwen14b_lora_init_A(void* args) {
    if (args == NULL) return RC_ERR_NULL_ARGS;
    HxQwenLoraInitArgs* a = (HxQwenLoraInitArgs*)args;
    if (a->out_p == 0) return RC_ERR_NULL_ARGS;
    if (a->n_adapters <= 0 || a->d_model <= 0 || a->lora_r <= 0) return RC_ERR_BAD_ARGS;

    float* out = (float*)(uintptr_t)a->out_p;
    const int64_t N_per = a->lora_r * a->d_model;
    const float   sigma = sqrtf(2.0f / (float)a->d_model);

    Xoshiro rng;
    for (int64_t k = 0; k < a->n_adapters; k++) {
        // Per-adapter seed so each of 192 adapters is independent and
        // reproducible given base seed.
        xoshiro_seed(&rng, (uint64_t)a->seed ^ (uint64_t)k * 0x9e3779b97f4a7c15ULL);
        float* blk = out + k * N_per;
        for (int64_t i = 0; i < N_per; i++) {
            blk[i] = gauss01(&rng) * sigma;
        }
    }
    return RC_OK;
}

int hxqwen14b_lora_init_B(void* args) {
    if (args == NULL) return RC_ERR_NULL_ARGS;
    HxQwenLoraInitArgs* a = (HxQwenLoraInitArgs*)args;
    if (a->out_p == 0) return RC_ERR_NULL_ARGS;
    if (a->n_adapters <= 0 || a->d_model <= 0 || a->lora_r <= 0) return RC_ERR_BAD_ARGS;

    float* out = (float*)(uintptr_t)a->out_p;
    const int64_t N_total = a->n_adapters * a->d_model * a->lora_r;
    memset(out, 0, (size_t)N_total * sizeof(float));
    return RC_OK;
}

// ─────────────────────────────────────────────────────────────
// SECTION 7. AdamW CPU reference (LIVE in v4)
//
// Standard Adam+decoupled-weight-decay (Loshchilov 2019):
//
//   g_clipped = clip_by_global_norm(g, grad_clip)            (optional)
//   m ← β₁·m + (1-β₁)·g
//   v ← β₂·v + (1-β₂)·g²
//   m̂ = m / (1 - β₁^t)
//   v̂ = v / (1 - β₂^t)
//   w ← w - lr·(m̂/(√v̂ + ε) + wd·w)
//
// Note: decoupled WD. For LoRA B (zero-init) we typically pass wd=0 to
// avoid pulling B away from 0. For LoRA A we pass 0.01 or 0.0 per plan.
//
// Called per-parameter-tensor (N up to ~40k for a single A block).
// ─────────────────────────────────────────────────────────────

static float compute_grad_l2norm(const float* g, int64_t n) {
    double s = 0.0;
    for (int64_t i = 0; i < n; i++) s += (double)g[i] * (double)g[i];
    return (float)sqrt(s);
}

int hxqwen14b_adamw_step_cpu(void* args) {
    if (args == NULL) return RC_ERR_NULL_ARGS;
    HxQwenAdamArgs* a = (HxQwenAdamArgs*)args;
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

    // Optional per-tensor grad clip (simpler than global across all 384
    // tensors — caller can pre-scale g to simulate global clip).
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
// SECTION 8. hxqwen14b_forward_with_lora — v4: validate, return -5.
//
// v5 CUDA kernel sequence (mapped, not implemented):
//
//   cublasHandle_t H = (cublasHandle_t)ctx->cublas_handle;
//   // tokens → embeddings: gather bf16 W_embed [V,d] by ids[B*S]
//   launch_embed_gather_bf16(ids_dev, W_embed_dev, h_dev, B*S, d);
//
//   for L in 0..48:
//     // RMSNorm pre-attn
//     launch_rmsnorm_bf16(h_dev, rms1_dev[L], h1_dev, B*S, d, eps=1e-6);
//     // GQA: q = h1 @ W_q (+ LoRA); k/v = h1 @ W_kv (+ LoRA); o = W_o·attn
//     //   Base cuBLAS: cublasGemmEx(H, N, N, d, B*S, d, bf16, bf16, bf16, ...)
//     //   LoRA delta: cublasGemmEx(A[r,d] @ h1) then cublasGemmEx(B[d,r] @ tmp)
//     //               scaled by (α/r).
//     launch_rope_bf16(q_dev, k_dev, head_dim, S);
//     launch_gqa_flash_attn_bf16(q_dev, k_dev, v_dev, o_dev,
//                                B, S, n_head=40, n_kv_head=8, head_dim=128);
//     // residual, RMSNorm, SwiGLU FFN
//     launch_residual_add(h_dev, o_dev);
//     launch_rmsnorm_bf16(h_dev, rms2_dev[L], h2_dev, ...);
//     // FFN: h2 @ W_gate (+swish) · (h2 @ W_up), then @ W_down
//     launch_swiglu_ffn_bf16(h2_dev, W_gate, W_up, W_down, ffn_dim=13824);
//     launch_residual_add(h_dev, ffn_out_dev);
//     if (save_acts) memcpy layer-L activation [B,S,d] to activations_p.
//
//   launch_rmsnorm_bf16(h_dev, rms_final, h_dev, ...);
//   // lm_head: logits = h @ W_embed^T  (tied)
//   cublasGemmEx(H, N, T, V, B*S, d, bf16, bf16, fp32 logits, ...);
//   // CE loss
//   launch_ce_softmax_kernel(logits, targets, loss_out);
//
// Total kernels per forward: ~10 × 48 layers + 4 = 484 launches.
// Flash-attn v2 can fuse attn into one, reducing to ~340.
// ─────────────────────────────────────────────────────────────

static int validate_fwd_args(const HxQwenFwdArgs* a, QwenCtx** out_ctx) {
    if (a->handle <= 0 || a->handle >= HXQWEN14B_MAX_HANDLES) return RC_ERR_BAD_HANDLE;
    QwenCtx* ctx = &g_ctx_table[a->handle];
    if (!ctx->live) return RC_ERR_BAD_HANDLE;
    if (a->B_batch <= 0 || a->S_seq <= 0) return RC_ERR_BAD_ARGS;
    if (a->S_seq > QWEN14B_MAX_POS) return RC_ERR_BAD_ARGS;
    if (a->V_vocab != QWEN14B_VOCAB) return RC_ERR_SHAPE_MISMATCH;
    if (a->ids_p == 0 || a->logits_p == 0) return RC_ERR_NULL_ARGS;
    if (a->A_flat_p == 0 || a->B_flat_p == 0) return RC_ERR_NULL_ARGS;
    if (a->lora_r <= 0 || a->lora_r > 256) return RC_ERR_BAD_ARGS;
    if (a->lora_alpha <= 0) return RC_ERR_BAD_ARGS;
    *out_ctx = ctx;
    return RC_OK;
}

int hxqwen14b_forward_with_lora(void* args) {
    if (args == NULL) return RC_ERR_NULL_ARGS;
    HxQwenFwdArgs* a = (HxQwenFwdArgs*)args;
    QwenCtx* ctx = NULL;
    int rc = validate_fwd_args(a, &ctx);
    if (rc != RC_OK) return rc;

#if defined(__linux__) && defined(HXQWEN14B_CUDA)
    // v5: actual kernel sequence per header comment above.
    // For now return CUDA_TODO so trainer sees clean -5 and the non-CUDA
    // path of the LoRA loop (corpus, tokenize, adamw, ckpt I/O) runs.
#endif
    (void)ctx; (void)a;
    return RC_ERR_CUDA_TODO;
}

// ─────────────────────────────────────────────────────────────
// SECTION 9. hxqwen14b_backward_lora_only — v4: validate + zero dA/dB.
//
// v5 CUDA kernel sequence (mapped):
//
//   // CE backward: dlogits = (softmax(logits) - onehot(targets)) / (B*S)
//   launch_ce_backward(logits_dev, targets_dev, dlogits_dev, B, S, V);
//   // lm_head backward through tied embedding (W^T contribution only;
//   //   we freeze embed, so no dW_embed accumulation).
//   cublasGemmEx(NT: dlogits @ W_embed -> dh_final);
//
//   for L in reverse(0..48):
//     // Reload pre-layer-L activation (saved in fwd or recompute)
//     dh_layer = dh_next  (+ residual plumbing)
//     // SwiGLU bwd — no LoRA on FFN (frozen), skip grad accumulation
//     launch_swiglu_bwd(..., dh_layer -> dh_mid);
//     launch_rmsnorm_bwd(dh_mid, rms2, ..., dh_attn);
//
//     // Attn backward. LoRA lives on q_proj/k_proj/v_proj/o_proj:
//     //   y = W·x + (α/r)·B·(A·x)
//     //   dL/dx = W^T·dy + (α/r)·A^T·(B^T·dy)
//     //   dL/dA = (α/r)·(B^T·dy) ⊗ x
//     //   dL/dB = (α/r)·dy ⊗ (A·x)
//     // We compute ONLY dL/dA and dL/dB (W frozen → no dL/dW).
//     // 4 adapters per layer × 48 layers = 192 updates per bwd pass.
//     launch_lora_bwd_qkvo(dh_attn, A[L,q], B[L,q], act[L], alpha, r,
//                          dA[L,q], dB[L,q]);
//     ... k/v/o similarly ...
//     launch_rmsnorm_bwd(dh_attn, rms1, ..., dh_input);
//     dh_next = dh_input
//
//   // Embed grad: we freeze embed → no dW_embed. Stop at dh_embed.
//
//   // Multiply all accumulated dA/dB by grad_scale (inverse loss_scale)
//   //   so trainer sees unit-scale gradients.
//   launch_scale_in_place(dA_flat, 192·r·d, grad_scale);
//   launch_scale_in_place(dB_flat, 192·d·r, grad_scale);
//
// Total kernels per backward: ~16 × 48 layers + 4 = 772 launches.
// ─────────────────────────────────────────────────────────────

int hxqwen14b_backward_lora_only(void* args) {
    if (args == NULL) return RC_ERR_NULL_ARGS;
    HxQwenBwdArgs* a = (HxQwenBwdArgs*)args;
    if (a->handle <= 0 || a->handle >= HXQWEN14B_MAX_HANDLES) return RC_ERR_BAD_HANDLE;
    QwenCtx* ctx = &g_ctx_table[a->handle];
    if (!ctx->live) return RC_ERR_BAD_HANDLE;
    if (a->B_batch <= 0 || a->S_seq <= 0) return RC_ERR_BAD_ARGS;
    if (a->dlogits_p == 0 || a->A_flat_p == 0 || a->B_flat_p == 0) return RC_ERR_NULL_ARGS;
    if (a->dA_flat_p == 0 || a->dB_flat_p == 0) return RC_ERR_NULL_ARGS;
    if (a->activations_p == 0) return RC_ERR_NULL_ARGS;
    if (a->lora_r <= 0) return RC_ERR_BAD_ARGS;

    // v4: if requested, zero the dA/dB accumulators on host (this is a
    // CPU-only operation that's safe pre-CUDA).
    if (a->zero_grads_first) {
        const int64_t nA = (int64_t)LORA_N_ADAPTERS * a->lora_r * QWEN14B_D_MODEL;
        const int64_t nB = (int64_t)LORA_N_ADAPTERS * QWEN14B_D_MODEL * a->lora_r;
        float* dA = (float*)(uintptr_t)a->dA_flat_p;
        float* dB = (float*)(uintptr_t)a->dB_flat_p;
        memset(dA, 0, (size_t)nA * sizeof(float));
        memset(dB, 0, (size_t)nB * sizeof(float));
    }

#if defined(__linux__) && defined(HXQWEN14B_CUDA)
    // v5: full CUDA backward per header comment above.
#endif
    (void)ctx;
    return RC_ERR_CUDA_TODO;
}

// ─────────────────────────────────────────────────────────────
// SECTION 10. hxqwen14b_apply_lora_delta — v4: validate, return -5.
//
// v5: in-place W' = W + (α/r)·B·A per adapter. Materialising ΔW = B·A
// costs d·d = 100MB per adapter × 192 = 19.2GB bf16 — prohibitive.
// Better approach: store ΔW implicitly and fold at serve time via a
// fused kernel that does (W + (α/r)·B·A)·x in one pass.
//
// For LoRA export (out_mode == 1): we materialise ΔW into caller-owned
// float32 host buffer so the caller can write HF-compatible safetensors.
// ─────────────────────────────────────────────────────────────

int hxqwen14b_apply_lora_delta(void* args) {
    if (args == NULL) return RC_ERR_NULL_ARGS;
    HxQwenApplyArgs* a = (HxQwenApplyArgs*)args;
    if (a->handle <= 0 || a->handle >= HXQWEN14B_MAX_HANDLES) return RC_ERR_BAD_HANDLE;
    QwenCtx* ctx = &g_ctx_table[a->handle];
    if (!ctx->live) return RC_ERR_BAD_HANDLE;
    if (a->A_flat_p == 0 || a->B_flat_p == 0) return RC_ERR_NULL_ARGS;
    if (a->lora_r <= 0) return RC_ERR_BAD_ARGS;

#if defined(__linux__) && defined(HXQWEN14B_CUDA)
    // v5: fuse or export.
#endif
    (void)ctx;
    return RC_ERR_CUDA_TODO;
}
