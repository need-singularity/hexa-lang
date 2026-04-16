// hxccl_linux.c — hexa FFI ↔ NCCL collective shim (Linux x86_64 / Mac stub)
//
// C9 roadmap — "Zero-3 / FSDP" Day-1 MVP. Companion to hxblas_linux.c,
// hxlayer_linux.c, and hxflash_linux.c. This shim wraps the handful of
// NCCL collectives that hexa-side FSDP / ZeRO-3 actually needs.
//
// Day-1 surface (two families, both exported):
//
//   Family A — buffer-in-place + dtype-encoded (original Day-1 shape;
//   consumed by fsdp.hexa / zero_shard.hexa for in-place AllGather /
//   ReduceScatter against a pre-sized recv buffer):
//     init(rank, world_size)
//     world_size() / rank()
//     all_gather(send, recv, count, dtype)
//     reduce_scatter(send, recv, count, dtype)
//     barrier()
//     version()
//
//   Family B — handle-based AllReduce/Broadcast (added by C9 Day-1 to
//   match NCCL's AllReduce+Broadcast surface so multi-GPU training
//   loops outside FSDP can use the shim directly):
//     hxccl_init(rank, world_size)            → opaque handle (int64)
//     hxccl_allreduce_float(h, buf, count, op)  op: 0=SUM, 1=AVG
//     hxccl_broadcast_float(h, buf, count, root)
//     hxccl_finalize(handle)
//
// The two families share the same Day-1 stub (no real NCCL calls yet —
// every collective is a single-rank memcpy or no-op); when HXCCL_NCCL
// is defined at compile time, Family B routes to ncclAllReduce /
// ncclBroadcast and Family A to ncclAllGather / ncclReduceScatter.
//
// Mac stub: the same C source compiles without __linux__, taking the
// stub branch at the bottom of the file. Every Family B collective
// returns -1 (so the caller can fall back to the in-process FSDP
// simulation); Family A functions become empty no-ops. init returns a
// non-null heap sentinel so finalize stays symmetric.
//
// The ZeRO-3 simulation lives in pure hexa:
//   self/ml/fsdp.hexa           (754L)
//   self/ml/zero_shard.hexa     (246L)
//   self/ml/zero_optimizer.hexa (538L)
//   self/ml/zero_plusplus.hexa  (737L)
//
// Those files already implement the math — Day-1 here only provides
// the FFI seam so the same code runs untouched with real collectives
// once the H100 pod has >1 rank.
//
// Build (Linux x86_64, Ubuntu/Debian):
//   hexa scripts/build_hxccl_linux.hexa
// Cross-verify (Mac):
//   hexa scripts/build_hxccl_linux.hexa --mac-xverify
// Mac-native live FFI:
//   hexa scripts/build_hxccl_linux.hexa --mac-native
//
// ABI convention (matches hxblas_linux.c / hxlayer_linux.c / hxflash_linux.c
// bit-for-bit):
//   * pointers are passed as int64_t (Hexa Pointer == uint64_t opaque)
//   * counts, ranks, world_size are int64_t
//   * dtype is int64_t — encoding:
//       0 = f32   (Day-1 supported)
//       1 = f16   (Day-2 stub)
//       2 = bf16  (Day-2 stub)
//
// Day-1 stub semantics (world_size == 1):
//   * all_gather      : memcpy send → recv (count elements)
//   * reduce_scatter  : memcpy send → recv (count elements)
//   * barrier         : no-op
//   * init fails if world_size != 1 (Day-1 stub cannot express >1 rank)
//
// .hexanoport marker convention:
//   This .c is a native shim (extern FFI boundary), not a compilation
//   target for the Hexa-to-C codegen. See hexa_cc.c.hexanoport.

#ifdef __linux__

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// ─────────────────────────────────────────────────────────────
// Process-local rank/world cache. The real NCCL path will store
// an ncclComm_t here alongside; the stub only needs the ints.
// ─────────────────────────────────────────────────────────────
static int64_t g_hxccl_rank       = -1;
static int64_t g_hxccl_world_size =  0;
static int64_t g_hxccl_inited     =  0;

// Day-1 element size table. f32 is the only live dtype; f16/bf16 are
// accepted by the ABI but treated as 2-byte elements for a pure memcpy.
static inline size_t hxccl_elem_size(int64_t dtype) {
    switch (dtype) {
        case 0: return 4;   // f32
        case 1: return 2;   // f16
        case 2: return 2;   // bf16
        default: return 4;  // Day-1 default
    }
}

// ─────────────────────────────────────────────────────────────
// hxccl_init — bring the rank/world cache into a known state.
//
// Day-1 contract:
//   world_size == 1 → return 0 (single-rank stub, all collectives are
//                               memcpy / no-op)
//   world_size  > 1 → return -1 (stub cannot express >1 rank; Day-2
//                               will route to ncclCommInitRank)
//   world_size  < 1 → return -2 (invalid)
//   rank out of range → return -3
// ─────────────────────────────────────────────────────────────
int64_t hxccl_init(int64_t rank, int64_t world_size) {
    if (world_size < 1) return -2;
    if (rank < 0 || rank >= world_size) return -3;

#ifdef HXCCL_NCCL
    // Day-2: ncclCommInitRank here. For now the macro is undefined so
    // we fall through to the stub regardless.
#endif

    if (world_size != 1) {
        // Day-1 stub only supports single-rank. Day-2 flips this.
        return -1;
    }

    g_hxccl_rank       = rank;
    g_hxccl_world_size = world_size;
    g_hxccl_inited     = 1;
    return 0;
}

int64_t hxccl_world_size(void) {
    return g_hxccl_world_size;
}

int64_t hxccl_rank(void) {
    return g_hxccl_rank;
}

// ─────────────────────────────────────────────────────────────
// hxccl_all_gather — gather `count` elements from each rank into a
// world_size * count buffer.
//
// Day-1 stub (world_size==1): recv[0..count] = send[0..count]
// Day-2: ncclAllGather(send, recv, count, dtype, comm, stream)
// ─────────────────────────────────────────────────────────────
void hxccl_all_gather(int64_t send_ptr, int64_t recv_ptr,
                      int64_t count, int64_t dtype) {
    if (count <= 0) return;
    const void* sp = (const void*)(uintptr_t)send_ptr;
    void*       rp = (void*      )(uintptr_t)recv_ptr;
    if (!sp || !rp) return;

#ifdef HXCCL_NCCL
    // Day-2: ncclAllGather(sp, rp, (size_t)count, dtype_to_nccl(dtype),
    //                      g_hxccl_comm, 0);
    // For now fall through.
#endif

    // Stub: single-rank — output layout is [rank0 = send_buf].
    const size_t bytes = (size_t)count * hxccl_elem_size(dtype);
    memcpy(rp, sp, bytes);
}

// ─────────────────────────────────────────────────────────────
// hxccl_reduce_scatter — element-wise sum across ranks, then scatter
// so each rank ends up with `count` elements (the full reduced buffer
// has world_size * count elements in).
//
// Day-1 stub (world_size==1): recv[0..count] = send[0..count]
//   (no reduction needed — only one rank contributes)
// Day-2: ncclReduceScatter(send, recv, count, dtype, ncclSum, comm)
// ─────────────────────────────────────────────────────────────
void hxccl_reduce_scatter(int64_t send_ptr, int64_t recv_ptr,
                          int64_t count, int64_t dtype) {
    if (count <= 0) return;
    const void* sp = (const void*)(uintptr_t)send_ptr;
    void*       rp = (void*      )(uintptr_t)recv_ptr;
    if (!sp || !rp) return;

#ifdef HXCCL_NCCL
    // Day-2: ncclReduceScatter(sp, rp, (size_t)count, dtype_to_nccl(dtype),
    //                          ncclSum, g_hxccl_comm, 0);
#endif

    // Stub: single-rank — reduction over {send} is just send itself.
    const size_t bytes = (size_t)count * hxccl_elem_size(dtype);
    memcpy(rp, sp, bytes);
}

// ─────────────────────────────────────────────────────────────
// hxccl_barrier — rank-wide synchronisation.
//
// Day-1 stub: no-op (single rank is its own barrier).
// Day-2: ncclCommFinalize + cudaStreamSynchronize style wait.
// ─────────────────────────────────────────────────────────────
void hxccl_barrier(void) {
#ifdef HXCCL_NCCL
    // Day-2: ncclGroupStart/End + wait on the current stream.
#endif
    // Stub: no-op.
    return;
}

// ─────────────────────────────────────────────────────────────
// ABI version probe — matches hxblas_version / hxvdsp_version /
// hxlayer_version / hxflash_version style. Start at 1; bump on any
// breaking signature change (e.g. Day-2 NCCL communicator handle in
// init, async/stream variants, broadcast/all_reduce additions).
// ─────────────────────────────────────────────────────────────
int64_t hxccl_version(void) {
    return 1;
}

// ══════════════════════════════════════════════════════════════
// Family B — handle-based AllReduce / Broadcast / Finalize
//
// Added by C9 Day-1 (2026-04-16). Signature matches the shape expected
// by NCCL (ncclAllReduce, ncclBroadcast). Day-1 delivers a single-rank
// stub: init returns a heap context sentinel, AllReduce/Broadcast are
// no-ops on world=1 (self-reduce is identity), and any world>1 request
// returns -1 so the caller can fall back to pure-hexa simulation.
// Day-2 flips on HXCCL_NCCL + routes to real NCCL/CUDA calls.
//
// Op codes (match self/ml/hxccl.hexa constants):
//   0 = SUM   (Day-1 stub supports; no reduction needed for world=1)
//   1 = AVG   (Day-1 stub divides by world_size; 1 → identity)
// ──────────────────────────────────────────────────────────────

typedef struct hxccl_handle {
    int32_t rank;
    int32_t world;
    int32_t magic;   /* 0x48584343 'HXCC' */
} hxccl_handle;

// Renamed from hxccl_init → hxccl_init_handle on 2026-04-16 to resolve
// Linux duplicate-symbol with Family A's stateless hxccl_init.
int64_t hxccl_init_handle(int64_t rank, int64_t world_size) {
    if (world_size < 1) return -1;
    if (rank < 0 || rank >= world_size) return -1;

#ifdef HXCCL_NCCL
    // Day-2: ncclCommInitRank via the rendezvous path. For Day-1 we
    // fall through to the single-rank stub regardless.
#endif

    if (world_size != 1) {
        // Day-1 stub: no real NCCL backend compiled in, so >1 rank
        // cannot complete a real collective. Caller should fall back
        // to the in-process FSDP simulation.
        return -1;
    }

    hxccl_handle* h = (hxccl_handle*)calloc(1, sizeof(hxccl_handle));
    if (!h) return -1;
    h->rank  = (int32_t)rank;
    h->world = (int32_t)world_size;
    h->magic = 0x48584343;

    // Also populate the Family A globals so the two APIs agree on state.
    g_hxccl_rank       = rank;
    g_hxccl_world_size = world_size;
    g_hxccl_inited     = 1;

    return (int64_t)(uintptr_t)h;
}

int64_t hxccl_allreduce_float(int64_t handle, int64_t buf, int64_t count, int64_t op) {
    hxccl_handle* h = (hxccl_handle*)(uintptr_t)handle;
    if (!h || h->magic != 0x48584343) return -1;
    if (count <= 0) return -1;
    if (op != 0 && op != 1) return -1;
    float* bp = (float*)(uintptr_t)buf;
    if (!bp) return -1;

#ifdef HXCCL_NCCL
    // Day-2: ncclAllReduce(bp, bp, count, ncclFloat, ncclSum, comm, stream);
    // For op==AVG, post-scale by 1/world or use ncclAvg (NCCL >= 2.14).
#endif

    // Stub: single-rank reduction is identity (sum of one copy is itself).
    if (h->world == 1) {
        if (op == 1 /* AVG */) {
            // 1/world = 1.0 → identity; loop omitted.
        }
        return 0;
    }
    return -1;
}

int64_t hxccl_broadcast_float(int64_t handle, int64_t buf, int64_t count, int64_t root) {
    hxccl_handle* h = (hxccl_handle*)(uintptr_t)handle;
    if (!h || h->magic != 0x48584343) return -1;
    if (count <= 0) return -1;
    if (root < 0 || root >= h->world) return -1;
    if (!(float*)(uintptr_t)buf) return -1;

#ifdef HXCCL_NCCL
    // Day-2: ncclBroadcast(buf, buf, count, ncclFloat, root, comm, stream);
#endif

    // Stub: single-rank broadcast is identity — root is the only rank.
    if (h->world == 1) return 0;
    return -1;
}

int64_t hxccl_finalize(int64_t handle) {
    hxccl_handle* h = (hxccl_handle*)(uintptr_t)handle;
    if (!h || h->magic != 0x48584343) return -1;

#ifdef HXCCL_NCCL
    // Day-2: ncclCommDestroy + cudaStreamDestroy here.
#endif

    h->magic = 0;
    free(h);

    g_hxccl_rank       = -1;
    g_hxccl_world_size =  0;
    g_hxccl_inited     =  0;
    return 0;
}

#else  /* ─────────── Mac / no-Linux stub path ─────────── */
// Same ABI surface so the Mac smoke test can exercise the call graph.
// Family A becomes empty no-ops; Family B returns -1 on any collective
// to signal the caller should fall back to the in-process simulation.

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct hxccl_handle_mac {
    int32_t rank;
    int32_t world;
    int32_t magic; /* 0x48584343 */
} hxccl_handle_mac;

int64_t hxccl_version(void) { return 1; }

// Mac Family A globals — mirror Linux structure.
static int64_t g_hxccl_rank_mac       = -1;
static int64_t g_hxccl_world_size_mac =  0;

int64_t hxccl_world_size(void) { return g_hxccl_world_size_mac; }
int64_t hxccl_rank(void) { return g_hxccl_rank_mac; }

/* Family A stateless init — returns 0 on success, negative on failure. */
int64_t hxccl_init(int64_t rank, int64_t world_size) {
    if (world_size < 1) return -2;
    if (rank < 0 || rank >= world_size) return -3;
    if (world_size != 1) return -1;  /* Mac stub: single-rank only */
    g_hxccl_rank_mac       = rank;
    g_hxccl_world_size_mac = world_size;
    return 0;
}

/* Family B handle init — renamed 2026-04-16 to match Linux ABI. */
int64_t hxccl_init_handle(int64_t rank, int64_t world_size) {
    if (world_size < 1 || rank < 0 || rank >= world_size) return -1;
    hxccl_handle_mac* h = (hxccl_handle_mac*)calloc(1, sizeof(hxccl_handle_mac));
    if (!h) return -1;
    h->rank  = (int32_t)rank;
    h->world = (int32_t)world_size;
    h->magic = 0x48584343;
    return (int64_t)(uintptr_t)h;
}

int64_t hxccl_allreduce_float(int64_t handle, int64_t buf, int64_t count, int64_t op) {
    (void)buf; (void)count; (void)op;
    hxccl_handle_mac* h = (hxccl_handle_mac*)(uintptr_t)handle;
    if (!h || h->magic != 0x48584343) return -1;
    if (h->world == 1) return 0; /* identity on single rank */
    return -1;
}

int64_t hxccl_broadcast_float(int64_t handle, int64_t buf, int64_t count, int64_t root) {
    (void)buf; (void)count; (void)root;
    hxccl_handle_mac* h = (hxccl_handle_mac*)(uintptr_t)handle;
    if (!h || h->magic != 0x48584343) return -1;
    if (h->world == 1) return 0;
    return -1;
}

int64_t hxccl_finalize(int64_t handle) {
    hxccl_handle_mac* h = (hxccl_handle_mac*)(uintptr_t)handle;
    if (!h || h->magic != 0x48584343) return -1;
    h->magic = 0;
    free(h);
    return 0;
}

/* Family A no-ops so the shim stays loadable on Mac for dlopen smoke. */
void hxccl_all_gather(int64_t send_ptr, int64_t recv_ptr,
                      int64_t count, int64_t dtype) {
    (void)send_ptr; (void)recv_ptr; (void)count; (void)dtype;
}
void hxccl_reduce_scatter(int64_t send_ptr, int64_t recv_ptr,
                          int64_t count, int64_t dtype) {
    (void)send_ptr; (void)recv_ptr; (void)count; (void)dtype;
}
void hxccl_barrier(void) {}

#endif /* __linux__ */
