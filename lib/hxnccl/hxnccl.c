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
extern int64_t hxccl_world_size(void);
extern int64_t hxccl_rank(void);
extern int64_t hxccl_version(void);

// Process-global handle so the user-facing surface stays stateless from
// the hexa side (matches training/nccl_ffi.hexa module-global pattern).
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
