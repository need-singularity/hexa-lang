// lib/hxnccl/smoke.c — single-rank smoke test for libhxnccl.
//
// Exercises init_world(0,1) → barrier → destroy_world. Verifies:
//   - hxnccl_init_world returns 0 for world_size=1 even when NCCL is
//     not present (Mac stub) or present (Linux Day-2).
//   - hxnccl_world_size() / hxnccl_rank() report the cached values.
//   - hxnccl_barrier() is a no-op for world=1 and returns 0.
//   - hxnccl_destroy_world() returns 0 and is idempotent.
//
// Multi-rank (world_size>=2) smoke is OUT OF SCOPE here — that requires
// 2 GPUs (H100 SXM pod, $5.98/hr). This smoke is the gate that the FFI
// surface, dlopen layout, and ABI are correct before we spend on a pod.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

extern int64_t hxnccl_init_world(int64_t rank, int64_t world_size,
                                 const char* master_addr, int64_t master_port);
extern int64_t hxnccl_destroy_world(void);
extern int64_t hxnccl_barrier(void);
extern int64_t hxnccl_world_size(void);
extern int64_t hxnccl_rank(void);
extern int64_t hxnccl_version(void);

static int fail_count = 0;

#define EXPECT_EQ(label, got, want) do { \
    int64_t _g = (int64_t)(got); \
    int64_t _w = (int64_t)(want); \
    if (_g == _w) { \
        printf("  PASS  %-32s got=%lld\n", (label), (long long)_g); \
    } else { \
        printf("  FAIL  %-32s got=%lld want=%lld\n", \
               (label), (long long)_g, (long long)_w); \
        fail_count++; \
    } \
} while (0)

int main(void) {
    printf("[hxnccl_smoke] starting single-rank smoke\n");
    EXPECT_EQ("hxnccl_version() >= 1", hxnccl_version() >= 1 ? 1 : 0, 1);

    EXPECT_EQ("init_world(0, 1)", hxnccl_init_world(0, 1, "127.0.0.1", 0), 0);
    EXPECT_EQ("world_size()", hxnccl_world_size(), 1);
    EXPECT_EQ("rank()", hxnccl_rank(), 0);

    EXPECT_EQ("barrier() returns 0", hxnccl_barrier(), 0);

    EXPECT_EQ("destroy_world() returns 0", hxnccl_destroy_world(), 0);
    EXPECT_EQ("destroy_world() idempotent", hxnccl_destroy_world(), 0);

    EXPECT_EQ("world_size() after destroy", hxnccl_world_size(), 0);

    if (fail_count == 0) {
        printf("[hxnccl_smoke] ALL PASS — single-rank ABI verified\n");
        return 0;
    }
    printf("[hxnccl_smoke] %d FAIL\n", fail_count);
    return 1;
}
