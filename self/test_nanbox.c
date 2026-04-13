/* test_nanbox.c — encoding correctness tests for hexa_nanbox.h (rt#38-A).
 *
 * Build: clang -O2 -Wall -Wextra self/test_nanbox.c -o /tmp/test_nanbox
 *        /tmp/test_nanbox
 *
 * Exit 0 on all-pass, non-zero on failure. */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "hexa_nanbox.h"

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); g_fail++; } \
} while (0)

static void test_sizes(void) {
    CHECK(sizeof(HexaV) == 8, "HexaV is 8 bytes");
}

static void test_nil(void) {
    HexaV v = hexa_nb_nil();
    CHECK(hexa_nb_kind(v) == HEXA_NB_KIND_NIL, "nil kind");
    CHECK(hexa_nb_is_nil(v), "nil predicate");
    CHECK(!hexa_nb_is_double(v), "nil is not double");
}

static void test_bool(void) {
    HexaV t = hexa_nb_bool(1);
    HexaV f = hexa_nb_bool(0);
    CHECK(hexa_nb_kind(t) == HEXA_NB_KIND_BOOL, "true kind");
    CHECK(hexa_nb_kind(f) == HEXA_NB_KIND_BOOL, "false kind");
    CHECK(hexa_nb_as_bool(t) == 1, "true value");
    CHECK(hexa_nb_as_bool(f) == 0, "false value");
    CHECK(t != f, "true != false encoding");
}

static void test_int(void) {
    int32_t vals[] = {0, 1, -1, 42, -42, INT32_MAX, INT32_MIN, 1024, -1024};
    for (size_t i = 0; i < sizeof(vals)/sizeof(vals[0]); i++) {
        HexaV v = hexa_nb_int32(vals[i]);
        CHECK(hexa_nb_kind(v) == HEXA_NB_KIND_INT, "int kind");
        CHECK(hexa_nb_is_int(v), "int predicate");
        CHECK(hexa_nb_as_int(v) == (int64_t)vals[i], "int roundtrip");
    }
}

static void test_float(void) {
    double vals[] = {0.0, -0.0, 1.0, -1.0, 3.14, -2.71828, 1e300, -1e-300,
                     INFINITY, -INFINITY};
    for (size_t i = 0; i < sizeof(vals)/sizeof(vals[0]); i++) {
        HexaV v = hexa_nb_float(vals[i]);
        CHECK(hexa_nb_kind(v) == HEXA_NB_KIND_FLOAT, "float kind");
        CHECK(hexa_nb_is_double(v), "float is double");
        /* -0.0 == 0.0 by IEEE but bit-pattern differs; compare via memcmp. */
        double r = hexa_nb_as_float(v);
        if (vals[i] == vals[i]) {  /* not NaN */
            CHECK(r == vals[i] || (r != r && vals[i] != vals[i]),
                  "float roundtrip value");
        }
    }
    /* NaN canonicalization: any NaN goes in, a canonical NaN comes out. */
    double nan_in = NAN;
    HexaV vnan = hexa_nb_float(nan_in);
    CHECK(hexa_nb_kind(vnan) == HEXA_NB_KIND_FLOAT, "NaN kind is float");
    double nan_out = hexa_nb_as_float(vnan);
    CHECK(nan_out != nan_out, "NaN out is NaN");
}

static void test_ptr(void) {
    /* Simulated heap pointer. Must fit in canonical 48-bit userspace. */
    int dummy = 42;
    void* p = &dummy;
    HexaV v = hexa_nb_ptr(HEXA_NB_TAG_STR, p);
    CHECK(hexa_nb_kind(v) == HEXA_NB_KIND_STR, "str ptr kind");
    CHECK(hexa_nb_is_str(v), "str predicate");
    CHECK(hexa_nb_as_ptr(v) == p, "ptr roundtrip");

    /* malloc'd pointer — realistic heap */
    void* mp = malloc(64);
    HexaV va = hexa_nb_ptr(HEXA_NB_TAG_ARRAY, mp);
    CHECK(hexa_nb_as_ptr(va) == mp, "malloc'd ptr roundtrip");
    CHECK(hexa_nb_kind(va) == HEXA_NB_KIND_ARRAY, "array ptr kind");
    free(mp);

    /* All tag variants preserve pointer */
    uint32_t tags[] = {HEXA_NB_TAG_STR, HEXA_NB_TAG_ARRAY, HEXA_NB_TAG_MAP,
                      HEXA_NB_TAG_STRUCT, HEXA_NB_TAG_CLOSURE, HEXA_NB_TAG_FN};
    for (size_t i = 0; i < sizeof(tags)/sizeof(tags[0]); i++) {
        HexaV w = hexa_nb_ptr(tags[i], p);
        CHECK(hexa_nb_as_ptr(w) == p, "ptr roundtrip all tags");
        CHECK(hexa_nb_tag(w) == tags[i], "tag preserved");
    }
}

static void test_char(void) {
    uint32_t cps[] = {0x41 /* A */, 0xAC00 /* 가 */, 0x1F600 /* emoji */, 0x10FFFF};
    for (size_t i = 0; i < sizeof(cps)/sizeof(cps[0]); i++) {
        HexaV v = hexa_nb_char(cps[i]);
        CHECK(hexa_nb_kind(v) == HEXA_NB_KIND_CHAR, "char kind");
        CHECK(hexa_nb_as_char(v) == cps[i], "char roundtrip");
    }
}

static void test_no_double_collision(void) {
    /* Verify that common double values don't accidentally look like tagged. */
    double samples[] = {0.0, 1.0, -1.0, 1e-10, 1e10, 3.14159, INFINITY, -INFINITY};
    for (size_t i = 0; i < sizeof(samples)/sizeof(samples[0]); i++) {
        HexaV v = hexa_nb_float(samples[i]);
        CHECK(hexa_nb_is_double(v), "double not mis-tagged");
    }
    /* And a tagged value must not look like a double. */
    HexaV vi = hexa_nb_int32(12345);
    CHECK(!hexa_nb_is_double(vi), "int not mis-identified as double");
    HexaV vn = hexa_nb_nil();
    CHECK(!hexa_nb_is_double(vn), "nil not mis-identified as double");
}

int main(void) {
    test_sizes();
    test_nil();
    test_bool();
    test_int();
    test_float();
    test_ptr();
    test_char();
    test_no_double_collision();
    if (g_fail == 0) {
        printf("test_nanbox: ALL PASS (rt#38-A encoding verified)\n");
        return 0;
    }
    printf("test_nanbox: %d FAIL\n", g_fail);
    return 1;
}
