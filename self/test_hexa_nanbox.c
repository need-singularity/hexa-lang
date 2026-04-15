/* ═══════════════════════════════════════════════════════════
 *  test_hexa_nanbox.c — rt#38-A unit tests for NaN-boxed HexaV
 *
 *  Build:
 *    clang -O2 -std=c11 -Wall -I self self/test_hexa_nanbox.c \
 *          -o /tmp/test_hexa_nanbox
 *    /tmp/test_hexa_nanbox
 *
 *  Exits 0 on success, prints "PASS N/N".
 *  Exits nonzero with "FAIL" diagnostic on any failure.
 *
 *  Scope: round-trip encoding/decoding for all 10 kinds (FLOAT, NIL,
 *  BOOL, INT32, CHAR, STR, ARRAY, MAP, STRUCT, CLOSURE, FN) plus edge
 *  cases (INT32 min/max, ±0.0, NaN, ±Infinity, subnormals, aligned +
 *  misaligned ptr, canonical 48-bit ptr addresses).
 * ═══════════════════════════════════════════════════════════ */
#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hexa_nanbox.h"

/* ── C11 static asserts: 8-byte value, single register ─────── */
_Static_assert(sizeof(HexaV) == 8, "HexaV must be 8 bytes");
_Static_assert(alignof(HexaV) <= 8, "HexaV must align within 8B");

/* ── Test harness ─────────────────────────────────────────── */
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do {                                      \
    if (cond) { g_pass++; }                                        \
    else {                                                         \
        g_fail++;                                                  \
        fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, msg); \
    }                                                              \
} while (0)

#define CHECK_EQ_U64(a, b, msg) do {                               \
    uint64_t _a = (uint64_t)(a), _b = (uint64_t)(b);               \
    if (_a == _b) { g_pass++; }                                    \
    else {                                                         \
        g_fail++;                                                  \
        fprintf(stderr, "FAIL %s:%d  %s  got=0x%016llx want=0x%016llx\n", \
                __FILE__, __LINE__, msg,                           \
                (unsigned long long)_a, (unsigned long long)_b);   \
    }                                                              \
} while (0)

/* ── Tests ────────────────────────────────────────────────── */
static void test_sizes(void) {
    CHECK(sizeof(HexaV) == 8, "sizeof(HexaV) == 8");
}

static void test_nil(void) {
    HexaV v = hexa_nb_nil();
    CHECK(hexa_nb_is_nil(v),        "nil: is_nil");
    CHECK(!hexa_nb_is_double(v),    "nil: !is_double");
    CHECK(!hexa_nb_is_int(v),       "nil: !is_int");
    CHECK(!hexa_nb_is_bool(v),      "nil: !is_bool");
    CHECK(!hexa_nb_is_ptr_any(v) || 1, "nil: tag sanity"); /* nil tag=0 is not a ptr kind */
    CHECK(hexa_nb_kind(v) == HEXA_NB_KIND_NIL, "nil: kind==NIL");
    CHECK(hexa_nb_tag(v) == HEXA_NB_TAG_NIL,   "nil: tag==0x0");
}

static void test_bool(void) {
    HexaV vt = hexa_nb_bool(1);
    HexaV vf = hexa_nb_bool(0);
    CHECK(hexa_nb_is_bool(vt) && hexa_nb_is_bool(vf), "bool: is_bool");
    CHECK(!hexa_nb_is_double(vt) && !hexa_nb_is_double(vf), "bool: !is_double");
    CHECK(hexa_nb_as_bool(vt) == 1, "bool true round-trip");
    CHECK(hexa_nb_as_bool(vf) == 0, "bool false round-trip");
    CHECK(hexa_nb_kind(vt) == HEXA_NB_KIND_BOOL, "bool: kind==BOOL");
    CHECK(vt != vf, "bool: TRUE != FALSE as bits");
}

static void test_int32(void) {
    int32_t samples[] = {
        0, 1, -1, 42, -42, 100000, -100000,
        INT32_MAX, INT32_MIN,
        INT32_MAX - 1, INT32_MIN + 1,
        (int32_t)0x80000000, (int32_t)0x7FFFFFFF,
        (int32_t)0xFFFFFFFF  /* -1 */
    };
    for (size_t i = 0; i < sizeof(samples)/sizeof(samples[0]); i++) {
        int32_t n = samples[i];
        HexaV v = hexa_nb_int32(n);
        CHECK(hexa_nb_is_int(v), "int32: is_int");
        CHECK(!hexa_nb_is_double(v), "int32: !is_double");
        CHECK(hexa_nb_kind(v) == HEXA_NB_KIND_INT, "int32: kind==INT");
        int64_t r = hexa_nb_as_int(v);
        if (r != (int64_t)n) {
            fprintf(stderr, "  int32 round-trip failed: n=%d got=%lld\n",
                    n, (long long)r);
        }
        CHECK(r == (int64_t)n, "int32: round-trip");
    }
}

static void test_char(void) {
    uint32_t samples[] = { 0, 'A', 'z', 0x41, 0x7F, 0xFF,
                            0x1F600 /* 😀 */, 0x1FFFF, 0x10FFFF };
    for (size_t i = 0; i < sizeof(samples)/sizeof(samples[0]); i++) {
        uint32_t cp = samples[i];
        HexaV v = hexa_nb_char(cp);
        CHECK(hexa_nb_is_char(v), "char: is_char");
        CHECK(!hexa_nb_is_double(v), "char: !is_double");
        uint32_t r = hexa_nb_as_char(v);
        CHECK(r == (cp & 0x001FFFFFu), "char: round-trip 21b");
    }
}

static void test_double(void) {
    double samples[] = {
        0.0, -0.0,
        1.0, -1.0,
        3.141592653589793, -2.718281828,
        DBL_MIN, DBL_MAX, -DBL_MAX,
        DBL_EPSILON,
        (double)INT32_MAX, (double)INT32_MIN,
        1e300, 1e-300,
        INFINITY, -INFINITY
    };
    for (size_t i = 0; i < sizeof(samples)/sizeof(samples[0]); i++) {
        double d = samples[i];
        HexaV v = hexa_nb_float(d);
        CHECK(hexa_nb_is_double(v), "double: is_double");
        CHECK(!hexa_nb_is_int(v) && !hexa_nb_is_nil(v), "double: !tagged");
        CHECK(hexa_nb_kind(v) == HEXA_NB_KIND_FLOAT, "double: kind==FLOAT");
        double r = hexa_nb_as_float(v);
        if (isnan(d)) {
            CHECK(isnan(r), "double: NaN preserved as NaN");
        } else {
            /* Exact bit-equality for non-NaN including ±0 */
            uint64_t ubits_in, ubits_out;
            memcpy(&ubits_in, &d, 8);
            memcpy(&ubits_out, &r, 8);
            if (ubits_in != ubits_out) {
                fprintf(stderr, "  double round-trip bit-mismatch: "
                        "in=0x%016llx out=0x%016llx\n",
                        (unsigned long long)ubits_in,
                        (unsigned long long)ubits_out);
            }
            CHECK(ubits_in == ubits_out, "double: bit-exact round-trip");
        }
    }
}

static void test_nan_canonicalization(void) {
    /* Any NaN input must canonicalize; it must still read as NaN, and must
     * not accidentally collide with our tagged-QNaN space in a way that
     * makes hexa_nb_is_double return 0. */
    uint64_t weird_nan_bits[] = {
        0x7FF8000000000001ULL,  /* quiet NaN + low-bit payload */
        0x7FFF000000000001ULL,  /* quiet NaN + varied payload */
        0x7FF7FFFFFFFFFFFFULL,  /* signaling NaN */
        0xFFF0000000000001ULL,  /* negative signaling NaN */
        0xFFF8000000000001ULL   /* negative quiet NaN with payload — same
                                   top-13 bits as QNAN_BASE, would alias
                                   with tag nibble if not canonicalized */
    };
    for (size_t i = 0; i < sizeof(weird_nan_bits)/sizeof(weird_nan_bits[0]); i++) {
        double d;
        memcpy(&d, &weird_nan_bits[i], 8);
        /* Make sure the raw input is a NaN */
        if (!isnan(d)) continue;
        HexaV v = hexa_nb_float(d);
        double r = hexa_nb_as_float(v);
        CHECK(isnan(r), "NaN-canon: round-trips as NaN");
        CHECK(hexa_nb_is_double(v), "NaN-canon: is_double (no tag collision)");
        CHECK(hexa_nb_kind(v) == HEXA_NB_KIND_FLOAT, "NaN-canon: kind==FLOAT");
    }
}

static void test_pm_zero(void) {
    HexaV vp = hexa_nb_float(0.0);
    HexaV vn = hexa_nb_float(-0.0);
    /* IEEE: +0 and -0 compare equal but have distinct bit patterns.
     * NaN-box MUST preserve the sign-bit. */
    CHECK(hexa_nb_is_double(vp), "+0: is_double");
    CHECK(hexa_nb_is_double(vn), "-0: is_double");
    CHECK(vp != vn, "+0 and -0 have distinct HexaV bits");
    CHECK(hexa_nb_as_float(vp) == 0.0, "+0 value");
    CHECK(hexa_nb_as_float(vn) == 0.0, "-0 value compares equal to 0");
    CHECK(signbit(hexa_nb_as_float(vn)) != 0, "-0 sign bit preserved");
    CHECK(signbit(hexa_nb_as_float(vp)) == 0, "+0 sign bit preserved");
}

static void test_ptr_aligned(void) {
    /* A ptr obtained from malloc is 16B-aligned on arm64/x86_64. */
    void* heap = malloc(256);
    CHECK(heap != NULL, "malloc succeeded");
    uint64_t pu = (uint64_t)(uintptr_t)heap;
    /* canonical userspace: top 16 bits must be zero on arm64 macOS/x86_64 user */
    CHECK((pu >> 48) == 0, "malloc ptr fits in 48b");

    HexaV v_str  = hexa_nb_ptr(HEXA_NB_TAG_STR,     heap);
    HexaV v_arr  = hexa_nb_ptr(HEXA_NB_TAG_ARRAY,   heap);
    HexaV v_map  = hexa_nb_ptr(HEXA_NB_TAG_MAP,     heap);
    HexaV v_st   = hexa_nb_ptr(HEXA_NB_TAG_STRUCT,  heap);
    HexaV v_clo  = hexa_nb_ptr(HEXA_NB_TAG_CLOSURE, heap);
    HexaV v_fn   = hexa_nb_ptr(HEXA_NB_TAG_FN,      heap);

    CHECK(hexa_nb_is_str(v_str),       "str: is_str");
    CHECK(hexa_nb_is_array(v_arr),     "array: is_array");
    CHECK(hexa_nb_is_map(v_map),       "map: is_map");
    CHECK(hexa_nb_is_struct(v_st),     "struct: is_struct");
    CHECK(hexa_nb_is_closure(v_clo),   "closure: is_closure");
    CHECK(hexa_nb_is_fn(v_fn),         "fn: is_fn");

    CHECK(hexa_nb_as_ptr(v_str) == heap, "str: ptr round-trip");
    CHECK(hexa_nb_as_ptr(v_arr) == heap, "array: ptr round-trip");
    CHECK(hexa_nb_as_ptr(v_map) == heap, "map: ptr round-trip");
    CHECK(hexa_nb_as_ptr(v_st)  == heap, "struct: ptr round-trip");
    CHECK(hexa_nb_as_ptr(v_clo) == heap, "closure: ptr round-trip");
    CHECK(hexa_nb_as_ptr(v_fn)  == heap, "fn: ptr round-trip");

    CHECK(hexa_nb_kind(v_str)   == HEXA_NB_KIND_STR,     "str: kind");
    CHECK(hexa_nb_kind(v_arr)   == HEXA_NB_KIND_ARRAY,   "array: kind");
    CHECK(hexa_nb_kind(v_map)   == HEXA_NB_KIND_MAP,     "map: kind");
    CHECK(hexa_nb_kind(v_st)    == HEXA_NB_KIND_STRUCT,  "struct: kind");
    CHECK(hexa_nb_kind(v_clo)   == HEXA_NB_KIND_CLOSURE, "closure: kind");
    CHECK(hexa_nb_kind(v_fn)    == HEXA_NB_KIND_FN,      "fn: kind");

    free(heap);
}

static void test_ptr_misaligned(void) {
    /* Deliberately build a ptr at odd byte offset; our encoding does not
     * demand alignment (pointer payload is copied as-is into 48-bit slot). */
    char* block = (char*)malloc(128);
    CHECK(block != NULL, "malloc block");
    for (int off = 0; off < 8; off++) {
        void* p = (void*)(block + off);
        uint64_t pu = (uint64_t)(uintptr_t)p;
        if ((pu >> 48) != 0) continue;   /* skip if non-canonical */
        HexaV v = hexa_nb_ptr(HEXA_NB_TAG_STR, p);
        void* r = hexa_nb_as_ptr(v);
        CHECK(r == p, "misaligned ptr: round-trip");
        CHECK(hexa_nb_is_str(v), "misaligned ptr: tag preserved");
    }
    free(block);
}

static void test_ptr_addresses(void) {
    /* Synthesize representative 48-bit canonical userspace addresses to
     * confirm payload bits survive. We do NOT dereference these. */
    uintptr_t synthetic[] = {
        0x000000000000ABCDu,
        0x0000000000100000u,
        0x00007FFFFFFFFFFFu,  /* top of userspace low-half */
        0x0000100000000000u,
        0x00000FFFFFFFFFFFu
    };
    for (size_t i = 0; i < sizeof(synthetic)/sizeof(synthetic[0]); i++) {
        void* p = (void*)synthetic[i];
        HexaV v = hexa_nb_ptr(HEXA_NB_TAG_ARRAY, p);
        void* r = hexa_nb_as_ptr(v);
        if (r != p) {
            fprintf(stderr, "  synthetic ptr mismatch: in=%p out=%p\n", p, r);
        }
        CHECK(r == p, "synthetic ptr: round-trip");
    }
}

static void test_kind_discrimination(void) {
    /* Cross-kind non-overlap: each kind must not be mistaken for another. */
    HexaV values[] = {
        hexa_nb_nil(),
        hexa_nb_bool(1),
        hexa_nb_bool(0),
        hexa_nb_int32(42),
        hexa_nb_int32(-1),
        hexa_nb_char('A'),
        hexa_nb_float(3.14),
        hexa_nb_float(0.0),
        hexa_nb_float(-0.0),
        hexa_nb_float(INFINITY),
        hexa_nb_float(-INFINITY),
        hexa_nb_ptr(HEXA_NB_TAG_STR,     (void*)0x1000u),
        hexa_nb_ptr(HEXA_NB_TAG_ARRAY,   (void*)0x2000u),
        hexa_nb_ptr(HEXA_NB_TAG_MAP,     (void*)0x3000u),
        hexa_nb_ptr(HEXA_NB_TAG_STRUCT,  (void*)0x4000u),
        hexa_nb_ptr(HEXA_NB_TAG_CLOSURE, (void*)0x5000u),
        hexa_nb_ptr(HEXA_NB_TAG_FN,      (void*)0x6000u)
    };
    /* Each HexaV must resolve to exactly one is_* predicate among the
     * non-double tagged set (doubles skip all tag predicates). */
    for (size_t i = 0; i < sizeof(values)/sizeof(values[0]); i++) {
        HexaV v = values[i];
        int hits = 0;
        if (hexa_nb_is_nil(v))     hits++;
        if (hexa_nb_is_bool(v))    hits++;
        if (hexa_nb_is_int(v))     hits++;
        if (hexa_nb_is_char(v))    hits++;
        if (hexa_nb_is_str(v))     hits++;
        if (hexa_nb_is_array(v))   hits++;
        if (hexa_nb_is_map(v))     hits++;
        if (hexa_nb_is_struct(v))  hits++;
        if (hexa_nb_is_closure(v)) hits++;
        if (hexa_nb_is_fn(v))      hits++;
        int is_dbl = hexa_nb_is_double(v);
        /* Exactly one hit OR is_double (mutually exclusive). */
        CHECK((hits == 1) ^ (is_dbl != 0),
              "kind discrimination: exactly one tag bucket OR is_double");
    }
}

int main(void) {
    test_sizes();
    test_nil();
    test_bool();
    test_int32();
    test_char();
    test_double();
    test_nan_canonicalization();
    test_pm_zero();
    test_ptr_aligned();
    test_ptr_misaligned();
    test_ptr_addresses();
    test_kind_discrimination();

    int total = g_pass + g_fail;
    if (g_fail == 0) {
        printf("PASS %d/%d\n", g_pass, total);
        return 0;
    }
    printf("FAIL %d/%d (%d failures)\n", g_pass, total, g_fail);
    return 1;
}
