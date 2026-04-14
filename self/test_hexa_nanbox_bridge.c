/* ═══════════════════════════════════════════════════════════
 *  test_hexa_nanbox_bridge.c — rt#38-A Phase 2 shadow-caller
 *
 *  Goal: prove the NaN-box encoding works end-to-end on live
 *  runtime data IN PARALLEL to the existing 32B HexaVal ABI.
 *
 *  Method:
 *    1) Shadow path — construct 10K ints via hexa_nb_shadow_int,
 *       round-trip through hexa_nb_shadow_as_int, verify values.
 *       Allocation counter in bridge header should read 0.
 *    2) Legacy path — call self/runtime.c's hexa_int 10K times +
 *       push each into a HexaVal array. Measure malloc/realloc
 *       bytes via an LD_PRELOAD-free shim (count items_size).
 *    3) Measure size: sizeof(HexaV)=8 vs sizeof(HexaVal)=32.
 *       Scale to an int array of 10K: 80KB vs 320KB.
 *
 *  Build:
 *    clang -O2 -std=c11 -Wall -Wextra -I self \
 *          self/test_hexa_nanbox_bridge.c self/runtime.c \
 *          -o /tmp/test_bridge
 *    /tmp/test_bridge
 *
 *  Exits 0 on success. Prints the alloc comparison line.
 *
 *  Scope guard: shadow path does NOT mutate existing HexaVal
 *  semantics. This test links against runtime.c only to exercise
 *  the legacy baseline side-by-side, not to replace it.
 * ═══════════════════════════════════════════════════════════ */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hexa_nanbox.h"
#include "hexa_nanbox_bridge.h"

/* ── Forward decls from self/runtime.c (legacy 32B HexaVal path) ─
 *
 *  We declare the minimum surface needed for the baseline
 *  comparison. The actual HexaVal layout is opaque here — we only
 *  need to call hexa_int/hexa_array_new/hexa_array_push and read
 *  back the .arr pointer via a hand-rolled accessor.
 *
 *  rt#38-A Phase 2 integration note: the struct layout below MUST
 *  mirror runtime.c's HexaVal. This is fragile. The forward decl
 *  of HexaTag/HexaVal is one of the integration conflicts
 *  documented in docs/rt-38-a-phase2.md — any rt#38-B refactor
 *  has to update both sides in lockstep.
 * ─────────────────────────────────────────────────────────────── */
typedef enum { TAG_INT = 0, TAG_FLOAT, TAG_BOOL, TAG_STR, TAG_VOID,
               TAG_ARRAY, TAG_MAP, TAG_FN, TAG_CHAR, TAG_CLOSURE,
               TAG_VALSTRUCT } HexaTag;

typedef struct HexaValStruct HexaValStruct;
typedef struct HexaMapTable HexaMapTable;

typedef struct HexaVal {
    HexaTag tag;
    union {
        int64_t i;
        double f;
        int b;
        char* s;
        struct { struct HexaVal* items; int len; int cap; } arr;
        struct { HexaMapTable* tbl; int len; } map;
        struct { void* fn_ptr; int arity; } fn;
        struct { void* fn_ptr; int arity; struct HexaVal* env_box; } clo;
        HexaValStruct* vs;
    };
} HexaVal;

extern HexaVal hexa_int(int64_t n);
extern HexaVal hexa_array_new(void);
extern HexaVal hexa_array_push(HexaVal arr, HexaVal item);

/* ── Test harness ────────────────────────────────────────── */
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do {                                      \
    if (cond) { g_pass++; }                                        \
    else {                                                         \
        g_fail++;                                                  \
        fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, msg); \
    }                                                              \
} while (0)

/* ── malloc shim — count bytes allocated during measured window ─
 *
 *  We can't LD_PRELOAD in a unit test, so we gate the counter via
 *  a manual flag + wrap malloc/realloc/calloc in the test logic.
 *  The legacy path, unfortunately, lives inside runtime.c which
 *  calls the system malloc directly — so we measure via a proxy:
 *  the final array's .cap field tells us how many HexaVal slots
 *  were reserved on the heap (sizeof(HexaVal) * cap bytes).
 *
 *  This is the same measurement strategy used in Phase-1 bench
 *  reports. See docs/rt-38-a-phase2.md "measurement approach".
 * ─────────────────────────────────────────────────────────────── */

/* ── Test 1: shadow-path round-trip (pure NaN-box) ──────────── */
#define N_INT 10000

static void test_shadow_roundtrip(void) {
    hexa_nb_shadow_reset();
    int64_t checksum_in  = 0;
    int64_t checksum_out = 0;
    HexaV sink = hexa_nb_shadow_nil();  /* keep compiler from hoisting loop */
    for (int64_t n = 0; n < N_INT; n++) {
        HexaV v = hexa_nb_shadow_int(n);
        sink   ^= v;
        checksum_in  += n;
        checksum_out += hexa_nb_shadow_as_int(v);
    }
    CHECK(sink != 0,                                   "shadow: sink accumulator non-zero");
    CHECK(checksum_in == checksum_out,                 "shadow: sum(n) == sum(as_int(int(n)))");
    CHECK(hexa_nb_shadow_int_count() == (uint64_t)N_INT, "shadow: ctor counter == N_INT");
    CHECK(hexa_nb_shadow_heap_alloc() == 0,            "shadow: ZERO heap alloc");
}

/* Boundary + negative + round-trip of selected ints */
static void test_shadow_edges(void) {
    int64_t samples[] = {0, 1, -1, 42, -42, 2147483647LL, -2147483648LL,
                         0xDEAD, -0xBEEF, 1000000, -1000000};
    for (size_t k = 0; k < sizeof(samples)/sizeof(samples[0]); k++) {
        HexaV v = hexa_nb_shadow_int(samples[k]);
        int64_t got = hexa_nb_shadow_as_int(v);
        CHECK(got == samples[k], "shadow edge: round-trip");
        CHECK(hexa_nb_is_int(v), "shadow edge: is_int");
    }
}

/* ── Test 2: legacy-path baseline (32B HexaVal array of 10K ints) ──
 *
 *  Measures:
 *    - sizeof(HexaVal) per slot
 *    - array.cap at end (heap-slot count)
 *    - total bytes reserved = sizeof(HexaVal) * cap
 *
 *  Compare against the NaN-box equivalent:
 *    - sizeof(HexaV) = 8  (see static assert)
 *    - hypothetical cap identical
 *    - total bytes = 8 * cap  (4x smaller)
 * ─────────────────────────────────────────────────────────────── */
static void test_legacy_baseline(size_t* out_legacy_bytes,
                                  size_t* out_shadow_bytes,
                                  int*    out_cap) {
    HexaVal arr = hexa_array_new();
    int64_t checksum = 0;
    for (int64_t n = 0; n < N_INT; n++) {
        arr = hexa_array_push(arr, hexa_int(n));
        checksum += n;
    }
    CHECK(arr.tag == TAG_ARRAY, "legacy: final tag == TAG_ARRAY");
    CHECK(arr.arr.len == N_INT, "legacy: len == N_INT");
    int abs_cap = arr.arr.cap < 0 ? -arr.arr.cap : arr.arr.cap;
    CHECK(abs_cap >= N_INT, "legacy: cap >= N_INT");

    /* Verify every slot preserved the int value. */
    int64_t readback = 0;
    for (int64_t n = 0; n < N_INT; n++) {
        HexaVal slot = arr.arr.items[n];
        CHECK(slot.tag == TAG_INT, "legacy: slot tag == TAG_INT");
        readback += slot.i;
    }
    CHECK(readback == checksum, "legacy: readback matches sum(n)");

    *out_cap = abs_cap;
    *out_legacy_bytes = (size_t)abs_cap * sizeof(HexaVal);
    *out_shadow_bytes = (size_t)abs_cap * sizeof(HexaV);
}

/* ── Main ────────────────────────────────────────────────── */
int main(void) {
    printf("══════════════════════════════════════════════════════════\n");
    printf(" rt#38-A Phase 2 shadow-caller bridge test\n");
    printf("══════════════════════════════════════════════════════════\n");

    printf("[sizes]\n");
    printf("  sizeof(HexaV)   = %zu bytes  (NaN-box)\n", sizeof(HexaV));
    printf("  sizeof(HexaVal) = %zu bytes  (legacy struct)\n", sizeof(HexaVal));
    printf("  ratio           = %.1fx\n", (double)sizeof(HexaVal) / (double)sizeof(HexaV));

    printf("\n[shadow round-trip: %d int values]\n", N_INT);
    test_shadow_roundtrip();
    printf("  SHADOW PATH: %llu heap alloc / %llu ctors\n",
           (unsigned long long)hexa_nb_shadow_heap_alloc(),
           (unsigned long long)hexa_nb_shadow_int_count());

    printf("\n[shadow edges]\n");
    test_shadow_edges();

    printf("\n[legacy baseline: HexaVal array of %d ints]\n", N_INT);
    size_t legacy_bytes = 0, shadow_bytes = 0;
    int cap = 0;
    test_legacy_baseline(&legacy_bytes, &shadow_bytes, &cap);
    printf("  LEGACY PATH: array cap=%d slots, reserved=%zu bytes\n", cap, legacy_bytes);
    printf("  SHADOW PATH: array cap=%d slots (hypothetical), reserved=%zu bytes\n", cap, shadow_bytes);
    printf("  Delta       : legacy - shadow = %zu bytes (%.1fx smaller)\n",
           legacy_bytes - shadow_bytes,
           (double)legacy_bytes / (double)shadow_bytes);

    printf("\n══════════════════════════════════════════════════════════\n");
    printf(" RESULT: PASS %d  FAIL %d\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════\n");

    return g_fail == 0 ? 0 : 1;
}
