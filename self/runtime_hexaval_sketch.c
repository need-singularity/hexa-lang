/* runtime_hexaval_sketch.c — RT-P3-1 design sketch (2026-04-14)
 * Minimal tagged-union targeting the 1377 gcc -Wint-conversion errors
 * produced by `long`-universal codegen in build/artifacts/hexa_native.c.
 * DESIGN SKETCH ONLY — see runtime_hexaval_DESIGN.md for migration + pitfalls. */
#ifndef HEXA_VAL_SKETCH_H
#define HEXA_VAL_SKETCH_H
#include <stdint.h>

typedef struct hexa_arr hexa_arr;   /* forward — defined in runtime.c */

typedef enum {
    HV_VOID = 0,
    HV_LONG = 1,   /* i64, also bool via 0/1 */
    HV_STR  = 2,   /* owned or literal const char* (borrow by default) */
    HV_ARR  = 3,   /* hexa_arr*  (dyn array of HexaVal) */
    HV_FLT  = 4,   /* double */
} HVTag;

typedef struct HexaVal {
    uint8_t tag;          /* HVTag; full byte avoids bitfield aliasing traps */
    uint8_t _pad[3];
    union {
        int64_t   i;      /* HV_LONG, HV_VOID (i=0) */
        double    f;      /* HV_FLT */
        char*     s;      /* HV_STR  — NUL-terminated, strdup'd or static literal */
        hexa_arr* a;      /* HV_ARR  — heap-owned by runtime */
    } u;
} HexaVal;                 /* sizeof == 16 on 64-bit (1+3 pad + 8 union + 4 tail pad) */

/* ── Constructors ─────────────────────────────────────────── */
static inline HexaVal hval_from_long(int64_t v)  { HexaVal r; r.tag = HV_LONG; r.u.i = v; return r; }
static inline HexaVal hval_from_flt (double  v)  { HexaVal r; r.tag = HV_FLT;  r.u.f = v; return r; }
static inline HexaVal hval_from_str (char*   s)  { HexaVal r; r.tag = HV_STR;  r.u.s = s; return r; }
static inline HexaVal hval_from_arr (hexa_arr* a){ HexaVal r; r.tag = HV_ARR;  r.u.a = a; return r; }
static inline HexaVal hval_void(void)            { HexaVal r; r.tag = HV_VOID; r.u.i = 0; return r; }

/* ── Accessors (asserting; return sentinel on mismatch) ─── */
static inline int64_t   hval_long(HexaVal v){ return (v.tag == HV_LONG) ? v.u.i : 0; }
static inline double    hval_flt (HexaVal v){ return (v.tag == HV_FLT)  ? v.u.f : 0.0; }
static inline char*     hval_str (HexaVal v){ return (v.tag == HV_STR)  ? v.u.s : (char*)""; }
static inline hexa_arr* hval_arr (HexaVal v){ return (v.tag == HV_ARR)  ? v.u.a : (hexa_arr*)0; }

/* ── Fusion helpers (subsume top error categories) ───────── */
/* hexa_arr_push currently `(long, long)` — 883 call sites pass string
 * literals cast implicitly to long. The fused helper takes a HexaVal
 * value and routes by tag into the existing long-based backing store. */
void hexa_arr_push_v(HexaVal arr, HexaVal val);    /* HV_ARR + any tag */

/* String equality fuses the `word == "if"` pointer-vs-integer warning
 * (Wpointer-integer-compare, 222 hits). Codegen emits hval_streq(word, lit). */
static inline int hval_streq(HexaVal a, HexaVal b) {
    if (a.tag != HV_STR || b.tag != HV_STR) return 0;
    extern int strcmp(const char*, const char*);
    return strcmp(a.u.s, b.u.s) == 0;
}
#endif /* HEXA_VAL_SKETCH_H */
