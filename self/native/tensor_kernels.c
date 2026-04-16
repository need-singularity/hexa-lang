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

/* @hot_kernel: f32/f64/i32 pointer read/write — direct memcpy ------ */

HexaVal hexa_ptr_write_f32(HexaVal ptr, HexaVal offset, HexaVal val) {
    int64_t p = HX_IS_INT(ptr) ? HX_INT(ptr) : 0;
    int64_t off = HX_IS_INT(offset) ? HX_INT(offset) : 0;
    if (p == 0) return hexa_void();
    float f = (float)__hx_to_double(val);
    memcpy((uint8_t*)(uintptr_t)p + off, &f, sizeof(float));
    return hexa_void();
}

HexaVal hexa_ptr_write_i32(HexaVal ptr, HexaVal offset, HexaVal val) {
    int64_t p = HX_IS_INT(ptr) ? HX_INT(ptr) : 0;
    int64_t off = HX_IS_INT(offset) ? HX_INT(offset) : 0;
    if (p == 0) return hexa_void();
    int32_t v = (int32_t)HX_INT(val);
    memcpy((uint8_t*)(uintptr_t)p + off, &v, sizeof(int32_t));
    return hexa_void();
}

HexaVal hexa_ptr_read_f64(HexaVal ptr, HexaVal offset) {
    int64_t p = HX_IS_INT(ptr) ? HX_INT(ptr) : 0;
    int64_t off = HX_IS_INT(offset) ? HX_INT(offset) : 0;
    if (p == 0) return hexa_float(0.0);
    double d;
    memcpy(&d, (uint8_t*)(uintptr_t)p + off, sizeof(double));
    return hexa_float(d);
}

HexaVal hexa_ptr_read_f32(HexaVal ptr, HexaVal offset) {
    int64_t p = HX_IS_INT(ptr) ? HX_INT(ptr) : 0;
    int64_t off = HX_IS_INT(offset) ? HX_INT(offset) : 0;
    if (p == 0) return hexa_float(0.0);
    float f;
    memcpy(&f, (uint8_t*)(uintptr_t)p + off, sizeof(float));
    return hexa_float((double)f);
}

HexaVal hexa_ptr_read_i32(HexaVal ptr, HexaVal offset) {
    int64_t p = HX_IS_INT(ptr) ? HX_INT(ptr) : 0;
    int64_t off = HX_IS_INT(offset) ? HX_INT(offset) : 0;
    if (p == 0) return hexa_int(0);
    int32_t v;
    memcpy(&v, (uint8_t*)(uintptr_t)p + off, sizeof(int32_t));
    return hexa_int((int64_t)v);
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
    int64_t p = HX_IS_INT(ptr) ? HX_INT(ptr) : 0;
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
    HexaTensorStub* t = (HexaTensorStub*)(uintptr_t)HX_INT(v);
    int64_t n = t->rows * t->cols;
    for (int64_t i = 0; i < n; i++) {
        double u = (rand()+1.0)/(RAND_MAX+1.0);
        double w = (rand()+1.0)/(RAND_MAX+1.0);
        t->data[i] = (float)(sqrt(-2.0*log(u)) * cos(6.28318530718*w));
    }
    return v;
}

HexaVal hexa_tensor_data_ptr(HexaVal tv) {
    HexaTensorStub* t = (HexaTensorStub*)(uintptr_t)HX_INT(tv);
    return hexa_int((int64_t)(uintptr_t)t->data);
}

HexaVal hexa_tensor_from_ptr(HexaVal p, HexaVal r, HexaVal c) {
    HexaTensorStub* t = (HexaTensorStub*)calloc(1, sizeof(*t));
    t->rows = HX_IS_INT(r)?HX_INT(r):(int64_t)HX_FLOAT(r);
    t->cols = HX_IS_INT(c)?HX_INT(c):(int64_t)HX_FLOAT(c);
    t->data = (float*)(uintptr_t)(HX_IS_INT(p)?HX_INT(p):(int64_t)HX_FLOAT(p));
    return hexa_int((int64_t)(uintptr_t)t);
}
