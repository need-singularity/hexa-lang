/* ═══════════════════════════════════════════════════════════
 *  test_hexa_nanbox_bridge.c — rt#38-A Phase 2+3 shadow-caller
 *
 *  Goal: prove the NaN-box encoding works end-to-end on live
 *  runtime data IN PARALLEL to the existing 32B HexaVal ABI.
 *
 *  Method:
 *    1) Shadow path (Phase 2 int + Phase 3 float) — construct
 *       10K ints / 10K floats via hexa_nb_shadow_int / _float,
 *       round-trip through _as_int / _as_float, verify values.
 *       Allocation counter in bridge header should read 0.
 *    2) Legacy path — call self/runtime.c's hexa_int / hexa_float
 *       10K times + push each into a HexaVal array. Measure
 *       malloc/realloc bytes via items_size proxy.
 *    3) Measure size: sizeof(HexaV)=8 vs sizeof(HexaVal)=32.
 *       Scale to an array of 10K: 80KB vs 320KB.
 *    4) Phase 3 edges: positive/negative/zero/subnormal,
 *       ±inf, NaN canonicalisation, integer-valued doubles.
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
 *  documented in doc/rt_38_a.md — any rt#38-B refactor
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
extern HexaVal hexa_float(double f);
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
 *  reports. See doc/rt_38_a.md "measurement approach".
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

/* ── Phase 3 Test: shadow-path float round-trip ─────────────── */
#define N_FLOAT 10000

/* Exact bit-pattern equality for doubles. Avoids == which is not
 * reflexive for NaN — we explicitly compare raw bit patterns so we
 * catch the "NaN canonicalisation" case too.
 *
 * On any IEEE-754 host (every platform hexa-lang targets), memcpy to
 * uint64_t is the well-defined way to extract the 64-bit representation
 * without aliasing UB. */
static uint64_t dbits(double d) {
    uint64_t u; memcpy(&u, &d, 8); return u;
}

static void test_shadow_float_roundtrip(void) {
    hexa_nb_shadow_reset();
    /* Generate a deterministic sweep across the finite double range:
     * we use a fused linear + multiplicative sequence so we hit both
     * tiny and large magnitudes within the 10K samples. */
    double checksum_in  = 0.0;
    double checksum_out = 0.0;
    uint64_t xor_sink = 0;  /* block compiler from hoisting the loop */
    for (int k = 0; k < N_FLOAT; k++) {
        /* Spread: k * 0.5 gives a half-integer base, multiplied by an
         * occasionally-negative factor — exercises sign bit, various
         * exponents, a couple of exactly-representable ints. */
        double x = ((k & 1) ? -1.0 : 1.0) * (0.5 * (double)k + (double)k * 1e-3);
        HexaV v = hexa_nb_shadow_float(x);
        double rt = hexa_nb_shadow_as_float(v);
        xor_sink ^= v;
        checksum_in  += x;
        checksum_out += rt;
        if (dbits(rt) != dbits(x)) {
            CHECK(0, "shadow float: bit-exact round-trip failed");
        }
    }
    /* All finite doubles survived bit-exactly, so sums must match. */
    CHECK(dbits(checksum_in) == dbits(checksum_out),
          "shadow float: checksum matches sum(x)");
    CHECK(xor_sink != 0,
          "shadow float: xor sink non-zero (loop not hoisted)");
    CHECK(hexa_nb_shadow_float_count() == (uint64_t)N_FLOAT,
          "shadow float: ctor counter == N_FLOAT");
    CHECK(hexa_nb_shadow_float_heap_alloc() == 0,
          "shadow float: ZERO heap alloc");
}

/* Phase 3 edge cases — the payload space where something might go wrong:
 *   - sign variants of zero (must NOT be conflated by encoder)
 *   - subnormals (smallest positive denormalised)
 *   - ±inf (NaN-box spec promises these pass through since exp==0x7FF
 *     but mantissa==0, so HEXA_NB_QNAN_MASK equality test fails)
 *   - NaN variants — these are canonicalised by hexa_nb_float, so the
 *     round-trip is weaker: we only require the result is still NaN,
 *     and that the canonical representation matches
 *     0x7FF8000000000000ULL as documented in hexa_nanbox.h:171-177.
 *   - integer-valued doubles near the int32 boundary — these could
 *     superficially resemble tagged INT32 values but for doubles the
 *     QNaN mask check must route them through the double path. */
static void test_shadow_float_edges(void) {
    double finite_samples[] = {
        0.0,
        -0.0,
        1.0,
        -1.0,
        3.14,
        -3.14,
        1e-308,              /* near subnormal boundary */
        2.2250738585072014e-308, /* DBL_MIN (smallest positive normal) */
        5e-324,              /* smallest positive subnormal */
        -5e-324,             /* smallest negative subnormal */
        1.7976931348623157e+308, /* DBL_MAX */
        -1.7976931348623157e+308,
        /* Integer-valued doubles — these must round-trip as doubles,
         * NOT be confused with tagged INT32. */
        3.0,
        -3.0,
        1e15,
        -1e15,
        2147483647.0,        /* int32 max as double */
        -2147483648.0,       /* int32 min as double */
        4294967295.0,        /* uint32 max as double */
        1e100                /* large but finite */
    };
    size_t n = sizeof(finite_samples)/sizeof(finite_samples[0]);
    for (size_t k = 0; k < n; k++) {
        double x = finite_samples[k];
        HexaV v = hexa_nb_shadow_float(x);
        double rt = hexa_nb_shadow_as_float(v);
        /* Finite doubles: bit-exact round-trip required. */
        CHECK(dbits(rt) == dbits(x), "shadow float edge: finite bit-exact");
        /* Tag predicate: these must read as double, not int/etc. */
        CHECK(!hexa_nb_is_int(v),  "shadow float edge: not INT32");
        CHECK(!hexa_nb_is_nil(v),  "shadow float edge: not NIL");
        CHECK(!hexa_nb_is_bool(v), "shadow float edge: not BOOL");
    }

    /* ±zero — specifically verify sign bit preservation. */
    {
        HexaV v_pz = hexa_nb_shadow_float(0.0);
        HexaV v_nz = hexa_nb_shadow_float(-0.0);
        double pz = hexa_nb_shadow_as_float(v_pz);
        double nz = hexa_nb_shadow_as_float(v_nz);
        CHECK(dbits(pz) == 0x0000000000000000ULL, "shadow float: +0.0 bits");
        CHECK(dbits(nz) == 0x8000000000000000ULL, "shadow float: -0.0 bits");
        CHECK(dbits(pz) != dbits(nz),              "shadow float: +0 != -0 in bits");
    }

    /* ±inf — exponent all-ones, mantissa zero. QNaN mask test:
     *   inf bits = 0x7FF0000000000000, QNaN base = 0xFFF8000000000000.
     *   (inf & QNaN_MASK) == 0x7FF0000000000000 != QNaN_MASK → is_double. */
    {
        double pinf = 1.0 / 0.0;
        double ninf = -1.0 / 0.0;
        HexaV v_pi = hexa_nb_shadow_float(pinf);
        HexaV v_ni = hexa_nb_shadow_float(ninf);
        double rt_pi = hexa_nb_shadow_as_float(v_pi);
        double rt_ni = hexa_nb_shadow_as_float(v_ni);
        CHECK(dbits(rt_pi) == dbits(pinf), "shadow float: +inf bit-exact");
        CHECK(dbits(rt_ni) == dbits(ninf), "shadow float: -inf bit-exact");
        CHECK(hexa_nb_kind(v_pi) == HEXA_NB_KIND_FLOAT, "shadow float: +inf kind=FLOAT");
        CHECK(hexa_nb_kind(v_ni) == HEXA_NB_KIND_FLOAT, "shadow float: -inf kind=FLOAT");
    }

    /* NaN canonicalisation — any NaN input maps to the canonical QNaN
     * 0x7FF8000000000000ULL. This is the documented collision-avoidance
     * contract (hexa_nanbox.h:169-177). The round-trip preserves "is NaN"
     * but not arbitrary NaN payloads.
     *
     * Host-NaN source: 0.0/0.0 is the reference quiet NaN.
     * We also construct a non-canonical NaN by setting a specific
     * payload pattern and verify it canonicalises, not collides. */
    {
        double host_nan = 0.0 / 0.0;
        HexaV v_n = hexa_nb_shadow_float(host_nan);
        /* Encoded value must be the canonical QNaN pattern. */
        CHECK(v_n == 0x7FF8000000000000ULL, "shadow float: NaN canonicalised to 0x7FF8..");
        double rt = hexa_nb_shadow_as_float(v_n);
        /* Result must still be NaN (any NaN pattern returns true for !=). */
        CHECK(rt != rt, "shadow float: NaN round-trip preserves NaN-ness");
        /* Canonical QNaN does NOT collide with our tagged QNaN namespace
         * because HEXA_NB_QNAN_MASK requires bit 63 = 1. Canonical QNaN
         * has bit 63 = 0 (positive), so is_double correctly returns 1. */
        CHECK(hexa_nb_is_double(v_n), "shadow float: canonical NaN is_double=1");
        CHECK(hexa_nb_kind(v_n) == HEXA_NB_KIND_FLOAT,
              "shadow float: canonical NaN kind=FLOAT");

        /* Non-canonical NaN input: assemble a quiet NaN with a non-zero
         * payload and a SIGNALING NaN (mantissa bit 51 = 0). Both must
         * canonicalise. */
        union { uint64_t u; double d; } ncn;
        ncn.u = 0x7FF8000000001234ULL;  /* QNaN with payload */
        HexaV v_nc = hexa_nb_shadow_float(ncn.d);
        CHECK(v_nc == 0x7FF8000000000000ULL,
              "shadow float: non-canonical QNaN → canonical");
        union { uint64_t u; double d; } snan;
        snan.u = 0x7FF0000000000001ULL; /* SNaN (exp all 1s, mantissa != 0, top bit 0) */
        HexaV v_s = hexa_nb_shadow_float(snan.d);
        CHECK(v_s == 0x7FF8000000000000ULL,
              "shadow float: signalling NaN → canonical");

        /* Finding: a NEGATIVE NaN (sign bit = 1) would share the
         * HEXA_NB_QNAN_MASK pattern — it is why the canonicaliser flips
         * to 0x7FF8.. (positive). Verify by constructing a negative NaN
         * and ensuring the canonicaliser defuses it. */
        union { uint64_t u; double d; } neg_nan;
        neg_nan.u = 0xFFF8000000001234ULL;  /* negative QNaN with payload */
        HexaV v_neg = hexa_nb_shadow_float(neg_nan.d);
        CHECK(v_neg == 0x7FF8000000000000ULL,
              "shadow float: negative NaN → canonical (collision averted)");
        CHECK(hexa_nb_is_double(v_neg),
              "shadow float: canonicalised negative NaN stays in double namespace");
    }
}

/* Phase 3 legacy baseline — HexaVal array of 10K floats.
 * Mirrors the Phase 2 int baseline, uses hexa_float + hexa_array_push. */
static void test_legacy_float_baseline(size_t* out_legacy_bytes,
                                        size_t* out_shadow_bytes,
                                        int*    out_cap) {
    HexaVal arr = hexa_array_new();
    double checksum = 0.0;
    for (int k = 0; k < N_FLOAT; k++) {
        double x = ((k & 1) ? -1.0 : 1.0) * (0.5 * (double)k + (double)k * 1e-3);
        arr = hexa_array_push(arr, hexa_float(x));
        checksum += x;
    }
    CHECK(arr.tag == TAG_ARRAY, "legacy float: final tag == TAG_ARRAY");
    CHECK(arr.arr.len == N_FLOAT, "legacy float: len == N_FLOAT");
    int abs_cap = arr.arr.cap < 0 ? -arr.arr.cap : arr.arr.cap;
    CHECK(abs_cap >= N_FLOAT, "legacy float: cap >= N_FLOAT");

    double readback = 0.0;
    for (int k = 0; k < N_FLOAT; k++) {
        HexaVal slot = arr.arr.items[k];
        CHECK(slot.tag == TAG_FLOAT, "legacy float: slot tag == TAG_FLOAT");
        readback += slot.f;
    }
    CHECK(dbits(readback) == dbits(checksum),
          "legacy float: readback matches sum(x)");

    *out_cap = abs_cap;
    *out_legacy_bytes = (size_t)abs_cap * sizeof(HexaVal);
    *out_shadow_bytes = (size_t)abs_cap * sizeof(HexaV);
}

/* ── Main ────────────────────────────────────────────────── */
int main(void) {
    printf("══════════════════════════════════════════════════════════\n");
    printf(" rt#38-A Phase 2+3 shadow-caller bridge test\n");
    printf("══════════════════════════════════════════════════════════\n");

    printf("[sizes]\n");
    printf("  sizeof(HexaV)   = %zu bytes  (NaN-box)\n", sizeof(HexaV));
    printf("  sizeof(HexaVal) = %zu bytes  (legacy struct)\n", sizeof(HexaVal));
    printf("  ratio           = %.1fx\n", (double)sizeof(HexaVal) / (double)sizeof(HexaV));

    printf("\n[Phase 2 — shadow round-trip: %d int values]\n", N_INT);
    test_shadow_roundtrip();
    printf("  SHADOW PATH: %llu heap alloc / %llu ctors\n",
           (unsigned long long)hexa_nb_shadow_heap_alloc(),
           (unsigned long long)hexa_nb_shadow_int_count());

    printf("\n[Phase 2 — shadow int edges]\n");
    test_shadow_edges();

    printf("\n[Phase 2 — legacy baseline: HexaVal array of %d ints]\n", N_INT);
    size_t legacy_bytes = 0, shadow_bytes = 0;
    int cap = 0;
    test_legacy_baseline(&legacy_bytes, &shadow_bytes, &cap);
    printf("  LEGACY PATH: array cap=%d slots, reserved=%zu bytes\n", cap, legacy_bytes);
    printf("  SHADOW PATH: array cap=%d slots (hypothetical), reserved=%zu bytes\n", cap, shadow_bytes);
    printf("  Delta       : legacy - shadow = %zu bytes (%.1fx smaller)\n",
           legacy_bytes - shadow_bytes,
           (double)legacy_bytes / (double)shadow_bytes);

    printf("\n[Phase 3 — shadow round-trip: %d float values]\n", N_FLOAT);
    test_shadow_float_roundtrip();
    printf("  SHADOW PATH: %llu heap alloc / %llu ctors\n",
           (unsigned long long)hexa_nb_shadow_float_heap_alloc(),
           (unsigned long long)hexa_nb_shadow_float_count());

    printf("\n[Phase 3 — shadow float edges: zero / subnormal / inf / NaN]\n");
    test_shadow_float_edges();

    printf("\n[Phase 3 — legacy baseline: HexaVal array of %d floats]\n", N_FLOAT);
    size_t fl_legacy_bytes = 0, fl_shadow_bytes = 0;
    int fl_cap = 0;
    test_legacy_float_baseline(&fl_legacy_bytes, &fl_shadow_bytes, &fl_cap);
    printf("  LEGACY PATH: array cap=%d slots, reserved=%zu bytes\n", fl_cap, fl_legacy_bytes);
    printf("  SHADOW PATH: array cap=%d slots (hypothetical), reserved=%zu bytes\n", fl_cap, fl_shadow_bytes);
    printf("  Delta       : legacy - shadow = %zu bytes (%.1fx smaller)\n",
           fl_legacy_bytes - fl_shadow_bytes,
           (double)fl_legacy_bytes / (double)fl_shadow_bytes);

    printf("\n══════════════════════════════════════════════════════════\n");
    printf(" RESULT: PASS %d  FAIL %d\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════\n");

    return g_fail == 0 ? 0 : 1;
}
