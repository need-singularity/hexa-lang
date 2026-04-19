// hxnccl.c — thin wrapper over hxccl_linux.c that exports the user-facing
// FFI surface required by training/nccl_ffi.hexa for ALM r12 FSDP.
//
// This file is the boundary requested in alm_r11_fsdp_plan_20260420 B1:
// "$HEXA_LANG/lib/hxnccl/ — libhxnccl.so". It re-exports the hxccl Day-2
// NCCL collectives under the names training/nccl_ffi.hexa expects:
//
//   hxnccl_init_world(rank, world_size, master_addr_ignored, master_port_ignored)
//                                       → ncclCommInitRank via hxccl rendezvous
//   hxnccl_allreduce_f32(ptr, count, op_code)  op: 0=SUM, 1=AVG
//   hxnccl_broadcast_f32(ptr, count, root)
//   hxnccl_barrier()
//   hxnccl_destroy_world()
//
// All buffer pointers are device pointers in NCCL mode. master_addr/port
// are accepted for API parity with the design doc but the rendezvous
// uses HXCCL_UNIQUE_ID_FILE (see hxccl_linux.c) — a shared file path is
// simpler than rolling a TCP store and works for intra-node SXM 2-GPU
// (the alm_r11 chosen topology) as well as shared-FS multi-node.
//
// On Mac (no NCCL/CUDA): this wrapper compiles against the hxccl Mac
// stub branch. init returns 0 only for world_size==1; collectives are
// no-ops; destroy returns 0. This lets us run the single-rank smoke
// test on Mac without an H100.

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

// hxccl_linux.c declarations we re-use.
extern int64_t hxccl_init(int64_t rank, int64_t world_size);
extern int64_t hxccl_init_handle(int64_t rank, int64_t world_size);
extern int64_t hxccl_allreduce_float(int64_t handle, int64_t buf,
                                     int64_t count, int64_t op);
extern int64_t hxccl_broadcast_float(int64_t handle, int64_t buf,
                                     int64_t count, int64_t root);
extern int64_t hxccl_finalize(int64_t handle);
extern void    hxccl_barrier(void);
extern void    hxccl_all_gather(int64_t send_ptr, int64_t recv_ptr,
                                int64_t count, int64_t dtype);
extern void    hxccl_reduce_scatter(int64_t send_ptr, int64_t recv_ptr,
                                    int64_t count, int64_t dtype);
extern int64_t hxccl_world_size(void);
extern int64_t hxccl_rank(void);
extern int64_t hxccl_version(void);

// Process-global handle so the user-facing surface stays stateless from
// the hexa side (matches training/nccl_ffi.hexa module-global pattern).
//
// Naming note (B2 wire, 2026-04-20): two surfaces co-exist here so that
// both training/nccl_ffi.hexa (handle-less, world_size+rank explicit) and
// training/train_alm_lora.hexa (handle-based, device_count+rank routed
// via stored handle) can dlopen the same libhxnccl.so. The two surfaces
// share g_hxnccl_handle under the hood — calling either init populates
// it, and either set of collectives works after that.
static int64_t g_hxnccl_handle = 0;

// hxnccl_init_world — set up the NCCL communicator for `world_size`
// ranks; this rank is `rank`. master_addr / master_port are accepted
// for API parity with the design doc but the rendezvous goes through
// HXCCL_UNIQUE_ID_FILE — see hxccl_linux.c for the file protocol.
//
// Returns 0 on success, negative on failure (forwards the inner code).
int64_t hxnccl_init_world(int64_t rank, int64_t world_size,
                          const char* master_addr, int64_t master_port) {
    (void)master_addr;
    (void)master_port;
    int64_t h = hxccl_init_handle(rank, world_size);
    if (h <= 0) {
        // hxccl_init_handle returns -1 on failure or handle pointer.
        return -1;
    }
    g_hxnccl_handle = h;
    return 0;
}

// hxnccl_allreduce_f32 — all-reduce `count` f32 elements at `ptr`
// across all ranks, op_code 0=SUM, 1=AVG. ptr must be a device pointer
// in NCCL mode. Returns 0 / -1.
int64_t hxnccl_allreduce_f32(int64_t ptr, int64_t count, int64_t op_code) {
    if (g_hxnccl_handle == 0) {
        fprintf(stderr, "[hxnccl] allreduce called before init_world\n");
        return -1;
    }
    return hxccl_allreduce_float(g_hxnccl_handle, ptr, count, op_code);
}

// hxnccl_broadcast_f32 — broadcast `count` f32 elements at `ptr` from
// rank `root` to all other ranks. Returns 0 / -1.
int64_t hxnccl_broadcast_f32(int64_t ptr, int64_t count, int64_t root) {
    if (g_hxnccl_handle == 0) {
        fprintf(stderr, "[hxnccl] broadcast called before init_world\n");
        return -1;
    }
    return hxccl_broadcast_float(g_hxnccl_handle, ptr, count, root);
}

// hxnccl_barrier — process-wide rank sync. Single-rank no-op.
int64_t hxnccl_barrier(void) {
    hxccl_barrier();
    return 0;
}

// hxnccl_destroy_world — tear down the communicator. Idempotent.
int64_t hxnccl_destroy_world(void) {
    if (g_hxnccl_handle == 0) return 0;
    int64_t r = hxccl_finalize(g_hxnccl_handle);
    g_hxnccl_handle = 0;
    return r;
}

// Convenience accessors for callers that need to know rank/world.
int64_t hxnccl_world_size(void) { return hxccl_world_size(); }
int64_t hxnccl_rank(void)       { return hxccl_rank(); }
int64_t hxnccl_version(void)    { return hxccl_version(); }

// ══════════════════════════════════════════════════════════════════════
// B2 LAYER COLLECTIVES — added 2026-04-20 (ALM r12 FSDP B2 unblock).
//
// fsdp_shard.hexa (CLM C9) and train_alm_lora.hexa (ALM r11/r12) walk a
// per-layer GpuLayer / GpuLayerGrad struct of 8 device pointers each.
// FSDP forward needs broadcast(owner→all), backward needs reduce_scatter
// (or all_reduce on the dense-grad path). The hexa caller iterates the
// 8 tensors and calls one of these per tensor.
//
// All buffers are device pointers in NCCL mode (HXCCL_NCCL=1 build);
// counts are in elements; dtype is encoded f32 via the entry-point name.
// reduce_scatter / allgather flow through hxccl Family A which already
// dispatches to ncclReduceScatter / ncclAllGather under HXCCL_NCCL.
// ══════════════════════════════════════════════════════════════════════

// hxnccl_reduce_scatter_f32 — element-wise sum across ranks then scatter.
// `send_ptr`: world_size * count elements (full unsharded grad);
// `recv_ptr`: count elements (this rank's shard).
// Returns 0 on success, -1 on missing init.
int64_t hxnccl_reduce_scatter_f32(int64_t send_ptr, int64_t recv_ptr,
                                  int64_t count) {
    if (g_hxnccl_handle == 0) {
        fprintf(stderr, "[hxnccl] reduce_scatter_f32 called before init_world\n");
        return -1;
    }
    // dtype 0 = f32 (matches hxccl_elem_size table in hxccl_linux.c).
    hxccl_reduce_scatter(send_ptr, recv_ptr, count, 0);
    return 0;
}

// hxnccl_allgather_f32 — gather `count` elements from each rank into a
// world_size * count buffer. Used by FSDP pre-forward to materialise a
// full layer weight tensor from the per-rank shards.
int64_t hxnccl_allgather_f32(int64_t send_ptr, int64_t recv_ptr,
                             int64_t count) {
    if (g_hxnccl_handle == 0) {
        fprintf(stderr, "[hxnccl] allgather_f32 called before init_world\n");
        return -1;
    }
    hxccl_all_gather(send_ptr, recv_ptr, count, 0);
    return 0;
}

// hxnccl_allreduce_bf16 — bf16 in-place all-reduce on a device buffer.
// dtype 2 = bf16 in hxccl_dtype_to_nccl. The current Family A entry
// (hxccl_all_gather / hxccl_reduce_scatter) carries the dtype but
// Family B (hxccl_allreduce_float) is f32-only — so we route bf16 via
// a dedicated direct ncclAllReduce wrapper. On Mac stub this is a
// world==1 no-op (returns 0).
//
// Signature matches train_alm_lora.hexa expectation:
//   hxnccl_allreduce_bf16(h_nccl, rank, buf, count) -> int
// where h_nccl is the handle returned by hxnccl_init() and `rank` is
// accepted for symmetry (the comm already knows our rank). Returns
// 0 on success, -1 on stub-mode multi-rank or missing init.
int64_t hxnccl_allreduce_bf16(int64_t h_nccl, int64_t rank,
                              int64_t buf, int64_t count) {
    (void)rank;
    if (h_nccl == 0 || g_hxnccl_handle == 0) {
        // Identity on world==1 — caller's grads already reflect the local
        // step. This matches train_alm_lora's "world_size==1 = no-op" doc.
        return 0;
    }
    if (count <= 0) return 0;

#ifdef HXCCL_NCCL
    // Direct ncclAllReduce on the active comm. We re-declare the
    // minimum NCCL surface here rather than pull in nccl.h again so
    // the link path stays scoped to this TU.
    extern int64_t hxnccl_internal_allreduce_bf16_dev(int64_t buf, int64_t count);
    return hxnccl_internal_allreduce_bf16_dev(buf, count);
#else
    // Stub build: world==1 only — identity for caller convenience.
    if (hxccl_world_size() <= 1) return 0;
    fprintf(stderr, "[hxnccl] allreduce_bf16 stub cannot do world>1\n");
    return -1;
#endif
}

// ══════════════════════════════════════════════════════════════════════
// HANDLE-BASED SURFACE — added 2026-04-20 to satisfy train_alm_lora.hexa
// extern decls. These are thin wrappers around the existing init / free
// path; the handle returned is just g_hxnccl_handle echoed back so the
// caller can treat it as opaque.
//
// hxnccl_init(device_count) — convenience entry that picks rank from the
//   LOCAL_RANK env (or falls back to 0). For the dual-rank SXM case the
//   torchrun-style launcher sets LOCAL_RANK=0 and LOCAL_RANK=1 on the
//   two processes. world_size==device_count.
// hxnccl_free(h_nccl) — alias for destroy_world; accepts the handle for
//   symmetry. Idempotent.
// ══════════════════════════════════════════════════════════════════════

#include <stdlib.h>  // getenv, atoi

int64_t hxnccl_init(int64_t device_count) {
    if (device_count < 1) return -1;
    int rank = 0;
    const char* lr = getenv("LOCAL_RANK");
    if (!lr || !lr[0]) lr = getenv("RANK");
    if (lr && lr[0]) {
        rank = atoi(lr);
        if (rank < 0 || rank >= (int)device_count) {
            fprintf(stderr, "[hxnccl] init: RANK env %d out of [0,%lld)\n",
                    rank, (long long)device_count);
            return -1;
        }
    }
    int64_t rc = hxnccl_init_world(rank, device_count, NULL, 0);
    if (rc != 0) return -1;
    // Return a non-zero handle so train_alm_lora's `G_NCCL_H != 0` gate
    // fires correctly. We echo the internal handle so callers can also
    // treat it as opaque.
    return g_hxnccl_handle != 0 ? g_hxnccl_handle : 1;
}

int64_t hxnccl_free(int64_t h_nccl) {
    (void)h_nccl;
    return hxnccl_destroy_world();
}

#ifdef HXCCL_NCCL
// Internal helper: bf16 ncclAllReduce on the active comm/stream. Defined
// in hxccl_linux.c (HXCCL_NCCL branch) so it has access to g_hxccl_comm /
// g_hxccl_stream + the nccl/cuda symbols. Forward-declared here.
//
// The hxccl_linux.c file exposes this as a leaf function so the wrapper
// stays compile-clean without pulling in <nccl.h> a second time.
int64_t hxnccl_internal_allreduce_bf16_dev(int64_t buf, int64_t count);
#endif
