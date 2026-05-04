// lib/hxpyembed/smoke.c — minimal C smoke test for libhxpyembed.
//
// Verifies the F-FFI-1 acceptance contract end-to-end without going
// through the hexa runtime: init → import torch → tensor zero-copy
// round-trip → finalize. Compile + run:
//
//   cc -O2 -Wall -I. smoke.c -L./build -lhxpyembed \
//      -Wl,-rpath,./build -o build/smoke
//   DYLD_LIBRARY_PATH=./build ./build/smoke
//
// Skips with exit 0 if torch is not installed.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern int64_t hxpy_init(void);
extern int64_t hxpy_finalize(void);
extern int64_t hxpy_is_initialized(void);
extern int64_t hxpy_eval(const char *code);
extern int64_t hxpy_call_str(const char *mod, const char *fn, const char *arg);
extern void    hxpy_free_str(int64_t p);
extern int64_t hxpy_tensor_to_py(void *raw, int64_t numel, const char *dtype);
extern int64_t hxpy_py_to_tensor(void *py_obj, void *out, int64_t max_elems);
extern int64_t hxpy_py_buf_addr(void *py_obj);
extern int64_t hxpy_py_buf_len(void *py_obj);
extern int64_t hxpy_py_buf_contig(void *py_obj);
extern void    hxpy_obj_decref(void *py_obj);

static int pass = 0, fail = 0;

#define CHK(tag, cond) do { \
    if (cond) { printf("  PASS %s\n", tag); pass++; } \
    else      { printf("  FAIL %s\n", tag); fail++; } \
} while (0)

int main(void) {
    printf("── hxpyembed smoke (F-FFI-1) ──\n");

    if (hxpy_init() != 0) {
        printf("  FAIL init\n");
        return 1;
    }
    CHK("init", hxpy_is_initialized() == 1);
    CHK("init_idempotent", hxpy_init() == 0);

    // Try to import torch; SKIP gracefully if not present.
    if (hxpy_eval("import torch") != 0) {
        printf("  SKIP torch_round_trip — torch not importable\n");
        hxpy_finalize();
        return 0;
    }
    CHK("import_torch", 1);

    // py_call("builtins", "str", "hello")
    int64_t s = hxpy_call_str("builtins", "str", "hello");
    CHK("call_str_hello", s != 0);
    if (s) {
        const char *p = (const char *)(uintptr_t)s;
        CHK("call_str_value", strcmp(p, "hello") == 0);
        hxpy_free_str(s);
    }

    // F-FFI-1: 1024-element f32 zero-copy round-trip.
    const int N = 1024;
    float *buf = (float *)malloc(N * sizeof(float));
    double sum_before = 0.0;
    for (int i = 0; i < N; i++) {
        buf[i] = (float)i * 0.5f;
        sum_before += buf[i];
    }

    int64_t mv = hxpy_tensor_to_py(buf, N, "f");
    CHK("tensor_to_py_handle", mv != 0);

    int64_t addr = hxpy_py_buf_addr((void *)(uintptr_t)mv);
    CHK("zero_copy_address_match", addr == (int64_t)(uintptr_t)buf);
    CHK("byte_length_4096", hxpy_py_buf_len((void *)(uintptr_t)mv) == N * 4);
    CHK("c_contiguous", hxpy_py_buf_contig((void *)(uintptr_t)mv) == 1);

    int64_t numel = hxpy_py_to_tensor((void *)(uintptr_t)mv, buf, N);
    CHK("py_to_tensor_numel_1024", numel == N);

    double sum_after = 0.0;
    for (int i = 0; i < N; i++) sum_after += buf[i];
    CHK("value_preservation", fabs(sum_after - sum_before) < 1e-3);
    CHK("expected_sum_261888", fabs(sum_before - 261888.0) < 0.5);

    hxpy_obj_decref((void *)(uintptr_t)mv);

    // Round-trip via numpy + memoryview.
    if (hxpy_eval("import numpy as np\n_arr = np.frombuffer(_mv := memoryview(bytearray(16)), dtype=np.float32) if False else None\n") == 0) {
        // not strictly needed — covered above. Keep the C smoke minimal.
    }

    free(buf);

    CHK("finalize", hxpy_finalize() == 0);
    CHK("post_finalize_not_init", hxpy_is_initialized() == 0);

    int total = pass + fail;
    printf("\n  pass=%d  fail=%d\n", pass, fail);
    if (fail == 0) {
        printf("PASS: hxpyembed smoke %d/%d\n", pass, total);
        return 0;
    }
    printf("FAIL: hxpyembed smoke %d/%d\n", pass, total);
    return 1;
}
