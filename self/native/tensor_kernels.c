// ═══════════════════════════════════════════════════════════
//  self/native/tensor_kernels.c — anima-critical HOT kernels (T3)
//
//  @hot_kernel — DO NOT MIGRATE TO HEXA
//  Every function here is called inside anima's training inner loop
//  (tensor init, forward/backward, optimizer). Moving to HEXA would
//  pay HexaVal-wrap/dispatch cost per element — 10-100x slowdown.
//
//  Boundary policy:
//    - OS calls (clock_gettime, rand) stay C
//    - f32/f64/i32 pointer read/write — C (direct memcpy, no HexaVal)
//    - tensor struct alloc / free / reshape — C
//    - BLAS adapters (if any) — C (link against Accelerate/OpenBLAS)
//
//  Whitelist: $NEXUS/shared/hexa/hot-path.jsonl (anima runtime consumers)
//  Policy:    docs/hexa-hot-path.md
//
//  Included into runtime.c via `#include "native/tensor_kernels.c"`.
//  Not a standalone TU — relies on runtime.c's types (HexaVal, TAG_INT,
//  TAG_FLOAT, hexa_int/hexa_float/hexa_void) + __hx_to_double helper.
// ═══════════════════════════════════════════════════════════

// Compatibility: HX_* accessor macros may not be defined yet if runtime.c
// is pre-Phase-A (struct fields only). Define them as identity-map stubs.
#ifndef HX_IS_INT
#define HX_IS_INT(v)    ((v).tag == TAG_INT)
#define HX_IS_FLOAT(v)  ((v).tag == TAG_FLOAT)
#define HX_INT(v)       ((v).i)
#define HX_FLOAT(v)     ((v).f)
#endif
#ifndef HX_INT_U
#define HX_INT_U(v)     HX_INT(v)
#endif

/* @hot_kernel: f32/f64/i32 pointer read/write — direct memcpy ------ */

HexaVal hexa_ptr_write_f32(HexaVal ptr, HexaVal offset, HexaVal val) {
    int64_t p = HX_IS_INT(ptr) ? HX_INT_U(ptr) : 0;
    int64_t off = HX_IS_INT(offset) ? HX_INT_U(offset) : 0;
    if (p == 0) return hexa_void();
    float f = (float)__hx_to_double(val);
    memcpy((uint8_t*)(uintptr_t)p + off, &f, sizeof(float));
    return hexa_void();
}

HexaVal hexa_ptr_write_i32(HexaVal ptr, HexaVal offset, HexaVal val) {
    int64_t p = HX_IS_INT(ptr) ? HX_INT_U(ptr) : 0;
    int64_t off = HX_IS_INT(offset) ? HX_INT_U(offset) : 0;
    if (p == 0) return hexa_void();
    int32_t v = (int32_t)HX_INT(val);
    memcpy((uint8_t*)(uintptr_t)p + off, &v, sizeof(int32_t));
    return hexa_void();
}

HexaVal hexa_ptr_read_f64(HexaVal ptr, HexaVal offset) {
    int64_t p = HX_IS_INT(ptr) ? HX_INT_U(ptr) : 0;
    int64_t off = HX_IS_INT(offset) ? HX_INT_U(offset) : 0;
    if (p == 0) return hexa_float(0.0);
    double d;
    memcpy(&d, (uint8_t*)(uintptr_t)p + off, sizeof(double));
    return hexa_float(d);
}

HexaVal hexa_ptr_read_f32(HexaVal ptr, HexaVal offset) {
    int64_t p = HX_IS_INT(ptr) ? HX_INT_U(ptr) : 0;
    int64_t off = HX_IS_INT(offset) ? HX_INT_U(offset) : 0;
    if (p == 0) return hexa_float(0.0);
    float f;
    memcpy(&f, (uint8_t*)(uintptr_t)p + off, sizeof(float));
    return hexa_float((double)f);
}

HexaVal hexa_ptr_read_i32(HexaVal ptr, HexaVal offset) {
    int64_t p = HX_IS_INT(ptr) ? HX_INT_U(ptr) : 0;
    int64_t off = HX_IS_INT(offset) ? HX_INT_U(offset) : 0;
    if (p == 0) return hexa_int(0);
    int32_t v;
    memcpy(&v, (uint8_t*)(uintptr_t)p + off, sizeof(int32_t));
    return hexa_int((int64_t)v);
}

/* IEEE-754 bit-cast helpers — bytes <-> Float (little-endian).
 * Required by safetensors B5 (encoder) and any pure-hexa caller that
 * needs the wire-level binary32/binary64 representation without an
 * external pointer allocation. Returns/accepts a TAG_ARRAY of int
 * (each element 0..255) — the project's canonical "bytes" shape.
 *
 * NaN handling: payload-preserving. The C `memcpy` round-trip copies
 * all 32/64 bits including the quiet/signaling bit and payload. We do
 * NOT canonicalize NaN — callers that need a single canonical NaN
 * must mask before encoding. Both signaling and quiet NaNs survive
 * a round-trip bit-identically (subject to platform float promotion;
 * see f32 narrowing note below).
 *
 * f32 narrowing note: hexa's runtime Float is C `double`. The
 * `f32_to_bytes_le` path performs a `(float)d` narrow which is
 * lossy for any double whose magnitude exceeds float range or whose
 * mantissa needs >24 bits. The reverse `bytes_to_f32_le` widens via
 * `(double)f` which IS lossless (every binary32 has an exact binary64
 * representation). Therefore round-trip is bit-identical ONLY for
 * values that already fit in binary32. Callers feeding arbitrary
 * doubles into f32_to_bytes_le accept the standard IEEE-754
 * round-to-nearest-even narrowing; ±inf and ±0 are preserved exactly,
 * subnormals/denormals follow the platform FPU mode (default IEEE).
 */

HexaVal hexa_f32_to_bytes_le(HexaVal val) {
    float f = (float)__hx_to_double(val);
    uint8_t buf[4];
    memcpy(buf, &f, 4);
    HexaVal arr = hexa_array_new();
    HexaVal* items = (HexaVal*)malloc(sizeof(HexaVal) * 4);
    for (int i = 0; i < 4; i++) items[i] = hexa_int((int64_t)buf[i]);
    HX_SET_ARR_ITEMS(arr, items);
    HX_SET_ARR_CAP(arr, 4);
    HX_SET_ARR_LEN(arr, 4);
    return arr;
}

HexaVal hexa_bytes_to_f32_le(HexaVal arr, HexaVal offset) {
    int64_t off = HX_IS_INT(offset) ? HX_INT(offset) : 0;
    if (!HX_IS_ARRAY(arr)) return hexa_float(0.0);
    int len = HX_ARR_LEN(arr);
    if (off < 0 || off + 4 > len) return hexa_float(0.0);
    HexaVal* items = HX_ARR_ITEMS(arr);
    uint8_t buf[4];
    for (int i = 0; i < 4; i++) {
        HexaVal el = items[off + i];
        int64_t v = 0;
        if (HX_IS_INT(el)) v = HX_INT(el);
        else if (HX_IS_FLOAT(el)) v = (int64_t)HX_FLOAT(el);
        buf[i] = (uint8_t)(v & 0xFF);
    }
    float f;
    memcpy(&f, buf, 4);
    return hexa_float((double)f);
}

HexaVal hexa_f64_to_bytes_le(HexaVal val) {
    double d = __hx_to_double(val);
    uint8_t buf[8];
    memcpy(buf, &d, 8);
    HexaVal arr = hexa_array_new();
    HexaVal* items = (HexaVal*)malloc(sizeof(HexaVal) * 8);
    for (int i = 0; i < 8; i++) items[i] = hexa_int((int64_t)buf[i]);
    HX_SET_ARR_ITEMS(arr, items);
    HX_SET_ARR_CAP(arr, 8);
    HX_SET_ARR_LEN(arr, 8);
    return arr;
}

HexaVal hexa_bytes_to_f64_le(HexaVal arr, HexaVal offset) {
    int64_t off = HX_IS_INT(offset) ? HX_INT(offset) : 0;
    if (!HX_IS_ARRAY(arr)) return hexa_float(0.0);
    int len = HX_ARR_LEN(arr);
    if (off < 0 || off + 8 > len) return hexa_float(0.0);
    HexaVal* items = HX_ARR_ITEMS(arr);
    uint8_t buf[8];
    for (int i = 0; i < 8; i++) {
        HexaVal el = items[off + i];
        int64_t v = 0;
        if (HX_IS_INT(el)) v = HX_INT(el);
        else if (HX_IS_FLOAT(el)) v = (int64_t)HX_FLOAT(el);
        buf[i] = (uint8_t)(v & 0xFF);
    }
    double d;
    memcpy(&d, buf, 8);
    return hexa_float(d);
}

/* Variant for the interpreter dispatch (TAG_VALSTRUCT-wrapped items).
 * Same semantics as hexa_bytes_to_f{32,64}_le but unwraps each Val
 * to read its int_val. Mirrors the rt_write_bytes_v / rt_write_bytes
 * split.
 */
HexaVal hexa_bytes_to_f32_le_v(HexaVal arr, HexaVal offset) {
    int64_t off = HX_IS_INT(offset) ? HX_INT(offset) : 0;
    if (!HX_IS_ARRAY(arr)) return hexa_float(0.0);
    int len = HX_ARR_LEN(arr);
    if (off < 0 || off + 4 > len) return hexa_float(0.0);
    HexaVal* items = HX_ARR_ITEMS(arr);
    uint8_t buf[4];
    for (int i = 0; i < 4; i++) {
        HexaVal el = items[off + i];
        int64_t v = 0;
        if (HX_IS_INT(el)) v = HX_INT(el);
        else if (HX_IS_FLOAT(el)) v = (int64_t)HX_FLOAT(el);
        else if (HX_TAG(el) == TAG_VALSTRUCT) {
            HexaVal iv = hexa_valstruct_int(el);
            if (HX_IS_INT(iv)) v = HX_INT(iv);
            else if (HX_IS_FLOAT(iv)) v = (int64_t)HX_FLOAT(iv);
        }
        buf[i] = (uint8_t)(v & 0xFF);
    }
    float f;
    memcpy(&f, buf, 4);
    return hexa_float((double)f);
}

HexaVal hexa_bytes_to_f64_le_v(HexaVal arr, HexaVal offset) {
    int64_t off = HX_IS_INT(offset) ? HX_INT(offset) : 0;
    if (!HX_IS_ARRAY(arr)) return hexa_float(0.0);
    int len = HX_ARR_LEN(arr);
    if (off < 0 || off + 8 > len) return hexa_float(0.0);
    HexaVal* items = HX_ARR_ITEMS(arr);
    uint8_t buf[8];
    for (int i = 0; i < 8; i++) {
        HexaVal el = items[off + i];
        int64_t v = 0;
        if (HX_IS_INT(el)) v = HX_INT(el);
        else if (HX_IS_FLOAT(el)) v = (int64_t)HX_FLOAT(el);
        else if (HX_TAG(el) == TAG_VALSTRUCT) {
            HexaVal iv = hexa_valstruct_int(el);
            if (HX_IS_INT(iv)) v = HX_INT(iv);
            else if (HX_IS_FLOAT(iv)) v = (int64_t)HX_FLOAT(iv);
        }
        buf[i] = (uint8_t)(v & 0xFF);
    }
    double d;
    memcpy(&d, buf, 8);
    return hexa_float(d);
}

/* @hot_kernel: f32/f64 struct pack/unpack (FFI marshaling) --------- */

HexaVal hexa_struct_pack_f32(HexaVal* args, int nargs) {
    if (nargs <= 0) return hexa_int(0);
    size_t total = (size_t)nargs * sizeof(float);
    float* buf = (float*)calloc(1, total);
    for (int i = 0; i < nargs; i++) {
        buf[i] = (float)(HX_IS_FLOAT(args[i]) ? HX_FLOAT(args[i]) : (double)HX_INT(args[i]));
    }
    return hexa_int((int64_t)(uintptr_t)buf);
}

HexaVal hexa_struct_unpack_f32(HexaVal ptr, HexaVal index) {
    int64_t p = HX_IS_INT(ptr) ? HX_INT_U(ptr) : 0;
    int64_t idx = HX_IS_INT(index) ? HX_INT(index) : 0;
    if (p == 0) return hexa_float(0.0);
    float* buf = (float*)(uintptr_t)p;
    return hexa_float((double)buf[idx]);
}

/* @hot_kernel: tensor struct stubs (row/col/data pointer) ---------- */

typedef struct { int64_t rows; int64_t cols; float* data; } HexaTensorStub;

HexaVal hexa_tensor_new(HexaVal r, HexaVal c) {
    int64_t rows = HX_IS_INT(r)?HX_INT(r):(int64_t)HX_FLOAT(r);
    int64_t cols = HX_IS_INT(c)?HX_INT(c):(int64_t)HX_FLOAT(c);
    HexaTensorStub* t = (HexaTensorStub*)calloc(1, sizeof(*t));
    t->rows = rows; t->cols = cols;
    t->data = (float*)calloc((size_t)(rows*cols), sizeof(float));
    return hexa_int((int64_t)(uintptr_t)t);
}

HexaVal hexa_tensor_randn(HexaVal r, HexaVal c) {
    HexaVal v = hexa_tensor_new(r, c);
    HexaTensorStub* t = (HexaTensorStub*)(uintptr_t)HX_INT_U(v);
    int64_t n = t->rows * t->cols;
    for (int64_t i = 0; i < n; i++) {
        double u = (rand()+1.0)/(RAND_MAX+1.0);
        double w = (rand()+1.0)/(RAND_MAX+1.0);
        t->data[i] = (float)(sqrt(-2.0*log(u)) * cos(6.28318530718*w));
    }
    return v;
}

HexaVal hexa_tensor_data_ptr(HexaVal tv) {
    HexaTensorStub* t = (HexaTensorStub*)(uintptr_t)HX_INT_U(tv);
    return hexa_int((int64_t)(uintptr_t)t->data);
}

HexaVal hexa_tensor_from_ptr(HexaVal p, HexaVal r, HexaVal c) {
    HexaTensorStub* t = (HexaTensorStub*)calloc(1, sizeof(*t));
    t->rows = HX_IS_INT(r)?HX_INT(r):(int64_t)HX_FLOAT(r);
    t->cols = HX_IS_INT(c)?HX_INT(c):(int64_t)HX_FLOAT(c);
    t->data = (float*)(uintptr_t)(HX_IS_INT(p)?HX_INT_U(p):(int64_t)HX_FLOAT(p));
    return hexa_int((int64_t)(uintptr_t)t);
}
