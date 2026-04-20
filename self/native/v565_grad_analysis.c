// v5.6.5 gradient SINGULARITY analysis harness — r12 acceleration lever extract.
//
// Goal: replicate Test 4 4-step CE descent (random ids, M=8, lora_r=8,
// alpha=16) and dump per-adapter (dA_L2, dB_L2) for all 192 adapters
// (48 layers × 4 projections {q,k,v,o}) at every step → 192-row CSV.
//
// CSV row format:
//   step,layer,proj,A_l2,B_l2,A_max,B_max,A_nan,B_nan
//
// Output: /workspace/r12_grad_192_adapters.csv
//
// Buffer layout (matches hxqwen14b_alloc_lora_buffers_v56):
//   384 slots = 192 adapters × 2 (A,B). adapter_idx = layer*4 + proj.
//   slot[adapter*2 + 0] = A pointer (R × K_in floats)
//   slot[adapter*2 + 1] = B pointer (N_out × R floats)
//
// Per Qwen 2.5 14B (D_MODEL=5120, num_heads=40, num_kv_heads=8, head_dim=128):
//   q_proj: out=5120, in=5120
//   k_proj: out=128*8=1024, in=5120
//   v_proj: out=1024, in=5120
//   o_proj: out=5120, in=5120
// All A buffers: R × 5120 = 40960 floats. B buffers: out × R = out * 8.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#include <math.h>
#include <time.h>

typedef int    (*fn_load)(int64_t, int64_t);
typedef int64_t (*fn_load_handle)(int64_t);
typedef int    (*fn_alloc_lora)(int64_t, int64_t);
typedef int    (*fn_free_lora)(int64_t);
typedef int    (*fn_alloc_cache)(int64_t, int64_t);
typedef int    (*fn_free_cache)(int64_t);
typedef int    (*fn_alloc_grad)(int64_t, int64_t);
typedef int    (*fn_free_grad)(int64_t);
typedef int    (*fn_fwd_cache)(int64_t, int64_t, int64_t, int64_t,
                                int64_t, int64_t, double, int64_t);
typedef int    (*fn_bwd)(int64_t, int64_t, int64_t, int64_t, int64_t,
                          int64_t, int64_t, int64_t, double);
typedef double (*fn_ce)(int64_t, int64_t, int64_t, int64_t);
typedef int64_t (*fn_v565)(void);
typedef int64_t (*fn_cu_malloc)(int64_t);
typedef int    (*fn_cu_free)(int64_t);
typedef int    (*fn_cu_h2d)(int64_t, void*, int64_t);
typedef int    (*fn_cu_d2h)(void*, int64_t, int64_t);
typedef int    (*fn_cu_sync)(void);
typedef int64_t (*fn_cache_total)(void);
typedef int    (*fn_memset)(int64_t, int64_t);
typedef int    (*fn_adamw_all)(int64_t, int64_t, int64_t, int64_t,
                                int64_t, int64_t,
                                double, double, double, double, double);

#define LOAD(name) do { \
    name = dlsym(h, #name); \
    if (!name) { fprintf(stderr, "dlsym %s failed: %s\n", #name, dlerror()); return 1; } \
} while(0)

#define VOCAB     152064
#define D_MODEL   5120
#define LORA_R    8
#define LORA_ALPHA 16.0
#define M_TOK     8
#define N_LAYERS  48
#define N_PROJ    4
#define N_ADAPT   (N_LAYERS * N_PROJ)
#define N_BUF     (N_ADAPT * 2)
#define N_STEP    4

static const char* PROJ_NAME[4] = {"q", "k", "v", "o"};

static int64_t proj_out_dim(int proj) {
    // q=5120, k=1024, v=1024, o=5120
    if (proj == 1 || proj == 2) return 1024;
    return 5120;
}

int main(int argc, char** argv) {
    const char* lib = "/workspace/hexa-lang/self/native/libhxqwen14b.so";
    const char* ckpt = "/workspace/Qwen2.5-14B-fp32-shards";
    const char* csv_path = "/workspace/r12_grad_192_adapters.csv";
    if (argc > 1) ckpt = argv[1];
    if (argc > 2) csv_path = argv[2];

    void* h = dlopen(lib, RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen failed: %s\n", dlerror()); return 1; }

    fn_v565 hxqwen14b_version_v565;
    fn_load_handle hxqwen14b_load;
    fn_load hxqwen14b_load_all_weights_pos;
    fn_alloc_lora hxqwen14b_alloc_lora_buffers_v56;
    fn_free_lora hxqwen14b_free_lora_buffers_v56;
    fn_alloc_cache hxqwen14b_v56_alloc_activation_cache;
    fn_free_cache hxqwen14b_v56_free_activation_cache;
    fn_alloc_grad hxqwen14b_v56_alloc_grad_buffers;
    fn_free_grad hxqwen14b_v56_free_grad_buffers;
    fn_fwd_cache hxqwen14b_base_forward_v56_with_lora_cache_pos;
    fn_bwd hxqwen14b_base_backward_v565_pos;
    fn_ce hxqwen14b_ce_loss_v54_pos;
    fn_cu_malloc hxqwen14b_cu_malloc_bytes;
    fn_cu_free hxqwen14b_cu_free_ptr;
    fn_cu_h2d hxqwen14b_cu_memcpy_h2d;
    fn_cu_d2h hxqwen14b_cu_memcpy_d2h;
    fn_cu_sync hxqwen14b_cu_device_sync;
    fn_cache_total hxqwen14b_v56_cache_total_slots;
    fn_memset hxqwen14b_cu_launch_memset_zero;
    fn_adamw_all hxqwen14b_v565_lora_adamw_step;

    LOAD(hxqwen14b_version_v565);
    LOAD(hxqwen14b_load);
    LOAD(hxqwen14b_load_all_weights_pos);
    LOAD(hxqwen14b_alloc_lora_buffers_v56);
    LOAD(hxqwen14b_free_lora_buffers_v56);
    LOAD(hxqwen14b_v56_alloc_activation_cache);
    LOAD(hxqwen14b_v56_free_activation_cache);
    LOAD(hxqwen14b_v56_alloc_grad_buffers);
    LOAD(hxqwen14b_v56_free_grad_buffers);
    LOAD(hxqwen14b_base_forward_v56_with_lora_cache_pos);
    LOAD(hxqwen14b_base_backward_v565_pos);
    LOAD(hxqwen14b_ce_loss_v54_pos);
    LOAD(hxqwen14b_cu_malloc_bytes);
    LOAD(hxqwen14b_cu_free_ptr);
    LOAD(hxqwen14b_cu_memcpy_h2d);
    LOAD(hxqwen14b_cu_memcpy_d2h);
    LOAD(hxqwen14b_cu_device_sync);
    LOAD(hxqwen14b_v56_cache_total_slots);
    LOAD(hxqwen14b_cu_launch_memset_zero);
    LOAD(hxqwen14b_v565_lora_adamw_step);

    printf("[grad] libhxqwen14b version=%lld\n",
        (long long)hxqwen14b_version_v565());

    int64_t handle = hxqwen14b_load((int64_t)(uintptr_t)ckpt);
    if (handle <= 0) { fprintf(stderr, "load handle=%lld\n", (long long)handle); return 2; }
    int rc = hxqwen14b_load_all_weights_pos(handle, (int64_t)(uintptr_t)ckpt);
    if (rc != 0) { fprintf(stderr, "load_all rc=%d\n", rc); return 2; }
    printf("[grad] handle=%lld weights loaded\n", (long long)handle);

    int64_t n_lora = N_BUF;   // 384
    int64_t n_cache = hxqwen14b_v56_cache_total_slots();
    int64_t* lora_ptrs  = (int64_t*)calloc(n_lora, sizeof(int64_t));
    int64_t* glora_ptrs = (int64_t*)calloc(n_lora, sizeof(int64_t));
    int64_t* cache_ptrs = (int64_t*)calloc(n_cache, sizeof(int64_t));
    int64_t* m_ptrs     = (int64_t*)calloc(n_lora, sizeof(int64_t));
    int64_t* v_ptrs     = (int64_t*)calloc(n_lora, sizeof(int64_t));

    rc = hxqwen14b_alloc_lora_buffers_v56(LORA_R, (int64_t)(uintptr_t)lora_ptrs);
    if (rc != 0) { fprintf(stderr, "alloc_lora rc=%d\n", rc); return 3; }
    rc = hxqwen14b_v56_alloc_grad_buffers(LORA_R, (int64_t)(uintptr_t)glora_ptrs);
    if (rc != 0) { fprintf(stderr, "alloc_grad rc=%d\n", rc); return 4; }
    rc = hxqwen14b_v56_alloc_grad_buffers(LORA_R, (int64_t)(uintptr_t)m_ptrs);
    if (rc != 0) { fprintf(stderr, "alloc_m rc=%d\n", rc); return 4; }
    rc = hxqwen14b_v56_alloc_grad_buffers(LORA_R, (int64_t)(uintptr_t)v_ptrs);
    if (rc != 0) { fprintf(stderr, "alloc_v rc=%d\n", rc); return 4; }
    rc = hxqwen14b_v56_alloc_activation_cache((int64_t)M_TOK, (int64_t)(uintptr_t)cache_ptrs);
    if (rc != 0) { fprintf(stderr, "alloc_cache rc=%d\n", rc); return 5; }

    // Zero AdamW moments (alloc_grad currently Kaiming-inits; we need 0).
    for (int idx = 0; idx < N_BUF; idx++) {
        int adapter = idx / 2;
        int proj    = adapter % 4;
        int is_A    = (idx % 2) == 0;
        int64_t Nout = proj_out_dim(proj);
        int64_t Kin  = D_MODEL;
        int64_t bytes = is_A ? (LORA_R * Kin * (int64_t)sizeof(float))
                             : (Nout * LORA_R * (int64_t)sizeof(float));
        if (m_ptrs[idx]) hxqwen14b_cu_launch_memset_zero(m_ptrs[idx], bytes);
        if (v_ptrs[idx]) hxqwen14b_cu_launch_memset_zero(v_ptrs[idx], bytes);
    }
    hxqwen14b_cu_device_sync();
    printf("[grad] LoRA + grad + cache + adam moments allocated\n");

    // Build M_TOK token ids — same scheme as v565_smoke (deterministic).
    int32_t ids_host[M_TOK];
    for (int i = 0; i < M_TOK; i++) ids_host[i] = (i * 7919 + 13) % VOCAB;
    int64_t ids_dev = hxqwen14b_cu_malloc_bytes(M_TOK * sizeof(int32_t));
    hxqwen14b_cu_memcpy_h2d(ids_dev, ids_host, M_TOK * sizeof(int32_t));

    int64_t logits_dev = hxqwen14b_cu_malloc_bytes(M_TOK * VOCAB * sizeof(float));
    int64_t tgt_full   = hxqwen14b_cu_malloc_bytes(M_TOK * sizeof(int32_t));
    int32_t tgt_full_host[M_TOK];
    for (int i = 0; i < M_TOK - 1; i++) tgt_full_host[i] = ids_host[i + 1];
    tgt_full_host[M_TOK - 1] = ids_host[0];
    hxqwen14b_cu_memcpy_h2d(tgt_full, tgt_full_host, M_TOK * sizeof(int32_t));

    int32_t tgt_short_host[M_TOK - 1];
    for (int i = 0; i < M_TOK - 1; i++) tgt_short_host[i] = ids_host[i + 1];
    int64_t tgt_short = hxqwen14b_cu_malloc_bytes((M_TOK - 1) * sizeof(int32_t));
    hxqwen14b_cu_memcpy_h2d(tgt_short, tgt_short_host, (M_TOK - 1) * sizeof(int32_t));

    // Open CSV.
    FILE* csv = fopen(csv_path, "w");
    if (!csv) { fprintf(stderr, "fopen %s\n", csv_path); return 20; }
    fprintf(csv, "step,layer,proj,A_l2,B_l2,A_max,B_max,A_nan,B_nan\n");

    // Host-side scratch — largest grad buffer is q/o B = 5120*8 = 40960 floats = 160KB.
    int max_buf_n = 5120 * LORA_R;
    if (LORA_R * D_MODEL > max_buf_n) max_buf_n = LORA_R * D_MODEL;
    float* host_buf = (float*)malloc(max_buf_n * sizeof(float));

    double lr_step = 1e-4;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int step = 1; step <= N_STEP; step++) {
        // Forward (with LoRA + cache).
        rc = hxqwen14b_base_forward_v56_with_lora_cache_pos(
            handle, ids_dev, M_TOK, logits_dev,
            (int64_t)(uintptr_t)lora_ptrs, LORA_R, LORA_ALPHA / (double)LORA_R,
            (int64_t)(uintptr_t)cache_ptrs);
        if (rc != 0) { fprintf(stderr, "step%d fwd rc=%d\n", step, rc); return 6; }

        // Backward — fills glora_ptrs[*] with gradients.
        rc = hxqwen14b_base_backward_v565_pos(
            handle, M_TOK, logits_dev, tgt_full,
            (int64_t)(uintptr_t)cache_ptrs,
            (int64_t)(uintptr_t)lora_ptrs,
            (int64_t)(uintptr_t)glora_ptrs,
            LORA_R, LORA_ALPHA / (double)LORA_R);
        if (rc != 0) { fprintf(stderr, "step%d bwd rc=%d\n", step, rc); return 7; }
        hxqwen14b_cu_device_sync();

        double ce_now = hxqwen14b_ce_loss_v54_pos(logits_dev, tgt_short, M_TOK - 1, VOCAB);
        printf("[grad] step=%d  CE=%.4f\n", step, ce_now);

        // Walk all 192 adapters, compute (A_l2, B_l2, A_max, B_max).
        for (int layer = 0; layer < N_LAYERS; layer++) {
            for (int proj = 0; proj < N_PROJ; proj++) {
                int adapter = layer * N_PROJ + proj;
                int slot_A = adapter * 2;
                int slot_B = adapter * 2 + 1;
                int64_t A_ptr = glora_ptrs[slot_A];
                int64_t B_ptr = glora_ptrs[slot_B];
                int64_t Nout = proj_out_dim(proj);
                int A_n = LORA_R * D_MODEL;
                int B_n = Nout * LORA_R;

                double A_l2 = 0, B_l2 = 0;
                double A_max = 0, B_max = 0;
                int A_nan = 0, B_nan = 0;

                if (A_ptr) {
                    hxqwen14b_cu_memcpy_d2h(host_buf, A_ptr, A_n * sizeof(float));
                    for (int i = 0; i < A_n; i++) {
                        float v = host_buf[i];
                        if (v != v) { A_nan++; continue; }
                        A_l2 += (double)v * v;
                        double av = fabs((double)v);
                        if (av > A_max) A_max = av;
                    }
                    A_l2 = sqrt(A_l2);
                }
                if (B_ptr) {
                    hxqwen14b_cu_memcpy_d2h(host_buf, B_ptr, B_n * sizeof(float));
                    for (int i = 0; i < B_n; i++) {
                        float v = host_buf[i];
                        if (v != v) { B_nan++; continue; }
                        B_l2 += (double)v * v;
                        double av = fabs((double)v);
                        if (av > B_max) B_max = av;
                    }
                    B_l2 = sqrt(B_l2);
                }

                fprintf(csv, "%d,%d,%s,%.6e,%.6e,%.6e,%.6e,%d,%d\n",
                    step, layer, PROJ_NAME[proj],
                    A_l2, B_l2, A_max, B_max, A_nan, B_nan);
            }
        }
        fflush(csv);

        // AdamW step (unblocks next step's CE descent).
        rc = hxqwen14b_v565_lora_adamw_step(
            (int64_t)(uintptr_t)lora_ptrs,
            (int64_t)(uintptr_t)glora_ptrs,
            (int64_t)(uintptr_t)m_ptrs,
            (int64_t)(uintptr_t)v_ptrs,
            LORA_R, /*step*/step,
            lr_step, /*beta1*/0.9, /*beta2*/0.95, /*eps*/1e-8, /*wd*/0.0);
        if (rc != 0) { fprintf(stderr, "step%d adamw rc=%d\n", step, rc); return 8; }
        hxqwen14b_cu_device_sync();
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double wallclock = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("[grad] 4-step grad-walk wallclock = %.2fs\n", wallclock);

    fclose(csv);
    printf("[grad] CSV written to %s (192 adapters × 4 steps = 768 rows)\n", csv_path);

    free(host_buf);
    hxqwen14b_cu_free_ptr(tgt_short);
    hxqwen14b_cu_free_ptr(tgt_full);
    hxqwen14b_cu_free_ptr(logits_dev);
    hxqwen14b_cu_free_ptr(ids_dev);
    hxqwen14b_v56_free_activation_cache((int64_t)(uintptr_t)cache_ptrs);
    hxqwen14b_v56_free_grad_buffers((int64_t)(uintptr_t)v_ptrs);
    hxqwen14b_v56_free_grad_buffers((int64_t)(uintptr_t)m_ptrs);
    hxqwen14b_v56_free_grad_buffers((int64_t)(uintptr_t)glora_ptrs);
    hxqwen14b_free_lora_buffers_v56((int64_t)(uintptr_t)lora_ptrs);
    free(lora_ptrs); free(glora_ptrs); free(cache_ptrs);
    free(m_ptrs); free(v_ptrs);
    dlclose(h);
    return 0;
}
