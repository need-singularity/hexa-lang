/* ═══════════════════════════════════════════════════════════
 *  hexa_nanbox.h — NaN-boxed HexaVal encoding (rt#38-A)
 *
 *  DESIGN-ONLY header for Tier-1 multi-session refactor rt#38.
 *  NOT yet wired into runtime.c — this is the encoding specification
 *  and reference implementation to be adopted in rt#38-B.
 *
 *  Goal: HexaVal (currently 32 bytes tagged-union struct) → 8 bytes
 *        uint64_t. Cache locality ~4x, scalar val_int/val_float
 *        becomes zero-alloc (no heap box, no struct-by-value).
 *
 *  Encoding (IEEE-754 double, QNaN payload carries non-double values):
 *
 *    63      62..52     51..48    47..0
 *     s  [11b exponent] [4b tag] [48b payload]
 *
 *    Real double:   exponent != 0x7FF OR (exponent==0x7FF AND mantissa==0)
 *                   i.e. non-NaN doubles pass through as-is.
 *    Signaling NaN: excluded (we reserve only QNaN space for tagging).
 *    Our QNaN base: 0xFFF8_0000_0000_0000
 *                   = sign(1) | exp(0x7FF) | QNaN-bit(1) | ...
 *
 *    Tag nibble bits 51..48 within QNaN:
 *      0x0 = NIL / VOID
 *      0x1 = BOOL         (payload: 0 or 1 in bit 0)
 *      0x2 = INT32        (payload: low 32 bits = int32_t, sign-extended on read)
 *      0x3 = CHAR         (payload: 21-bit unicode code point)
 *      0x4 = STR ptr      (payload: 48-bit heap pointer to HexaStr*)
 *      0x5 = ARRAY ptr    (payload: 48-bit heap pointer to HexaArr*)
 *      0x6 = MAP ptr      (payload: 48-bit heap pointer to HexaMap*)
 *      0x7 = STRUCT ptr   (payload: 48-bit heap pointer to HexaValStruct*)
 *      0x8 = CLOSURE ptr  (payload: 48-bit heap pointer to HexaClo*)
 *      0x9 = FN ptr       (payload: 48-bit heap pointer to HexaFn*)
 *      0xA..0xF reserved (big-int, bigfloat, symbol, weakref, future)
 *
 *  Assumptions:
 *    - x86_64 and ARM64 userspace pointers fit in 48 bits (canonical form).
 *    - int64_t storage requires heap boxing (beyond int32 range) — same as V8/JSC.
 *    - 'double' values that happen to be NaN must be canonicalized to the QNaN
 *       base pattern before re-entering HexaVal space (see hexa_nb_from_double).
 *
 *  API contract (this header is C99 header-only, static inline):
 *    HexaV   v = hexa_nb_int(42);
 *    HexaV   v = hexa_nb_float(3.14);
 *    HexaV   v = hexa_nb_bool(1);
 *    int     k = hexa_nb_kind(v);
 *    int64_t i = hexa_nb_as_int(v);
 *    double  f = hexa_nb_as_float(v);
 *    void*   p = hexa_nb_as_ptr(v);   // type-erased — caller must match tag
 *
 *  Migration note: rt#38-B will redefine `typedef uint64_t HexaVal;` and
 *  replace every `.tag == TAG_X` / `.i` / `.f` / `.s` / `.arr` / `.map` /
 *  `.fn` / `.clo` access site. Estimated ~2100 sites across 6 C files
 *  (runtime.c, hexa_cc.c, hexa_v2 transpiler outputs).
 * ═══════════════════════════════════════════════════════════ */
#ifndef HEXA_NANBOX_H
#define HEXA_NANBOX_H

#include <stdint.h>
#include <string.h>  /* memcpy */

/* Use a distinct type name (HexaV) until rt#38-B flips the typedef. */
typedef uint64_t HexaV;

/* ── Bit layout constants ─────────────────────────────────── */
/* QNaN base: sign=1, exp=all-ones (11 bits), quiet bit (51)=1.
 * Base pattern 0xFFF8_0000_0000_0000 identifies "tagged non-double".
 *
 *   63 | 62..52 | 51  | 50..47 | 46..0
 *    1 |  all 1 |  1  |  4bTAG |  47b payload
 *
 * We check via MASK==BASE where MASK covers bits 63..51 (13 bits) — any real
 * double fails this because either exp != 0x7FF, or exp == 0x7FF but mantissa
 * top bit (51) is 0 (signaling NaN) or all mantissa bits are 0 (infinity).
 * Canonical QNaN from C produces mantissa bit 51 = 1 with other bits 0, which
 * matches our base exactly and is safely claimed by tag 0x0 (NIL) slot; we
 * canonicalize NaN floats in hexa_nb_float() so they never reach this space.
 */
#define HEXA_NB_QNAN_BASE    0xFFF8000000000000ULL
#define HEXA_NB_QNAN_MASK    0xFFF8000000000000ULL  /* bits 63..51 must all be 1 */
#define HEXA_NB_TAG_MASK     0x0007800000000000ULL  /* bits 50..47 (4 bits) */
#define HEXA_NB_TAG_SHIFT    47
#define HEXA_NB_PAYLOAD_MASK 0x00007FFFFFFFFFFFULL  /* bits 46..0 (47 bits) */

/* Tag nibbles (4 bits, 0..15) */
#define HEXA_NB_TAG_NIL      0x0
#define HEXA_NB_TAG_BOOL     0x1
#define HEXA_NB_TAG_INT32    0x2
#define HEXA_NB_TAG_CHAR     0x3
#define HEXA_NB_TAG_STR      0x4
#define HEXA_NB_TAG_ARRAY    0x5
#define HEXA_NB_TAG_MAP      0x6
#define HEXA_NB_TAG_STRUCT   0x7
#define HEXA_NB_TAG_CLOSURE  0x8
#define HEXA_NB_TAG_FN       0x9

/* Kind enum (compatible with existing HexaTag numbering where possible). */
typedef enum {
    HEXA_NB_KIND_FLOAT = 0,   /* real IEEE double (non-NaN or canonical NaN) */
    HEXA_NB_KIND_NIL,
    HEXA_NB_KIND_BOOL,
    HEXA_NB_KIND_INT,
    HEXA_NB_KIND_CHAR,
    HEXA_NB_KIND_STR,
    HEXA_NB_KIND_ARRAY,
    HEXA_NB_KIND_MAP,
    HEXA_NB_KIND_STRUCT,
    HEXA_NB_KIND_CLOSURE,
    HEXA_NB_KIND_FN
} HexaNbKind;

/* ── bit-level helpers (double ↔ uint64, no UB via memcpy) ─── */
static inline uint64_t hexa_nb__d2u(double d) {
    uint64_t u; memcpy(&u, &d, 8); return u;
}
static inline double hexa_nb__u2d(uint64_t u) {
    double d; memcpy(&d, &u, 8); return d;
}

/* ── Predicates ───────────────────────────────────────────── */
static inline int hexa_nb_is_double(HexaV v) {
    /* A real double has at most 12 leading bits set to 1 with exp==0x7FF
     * AND the QNaN bit 0 — i.e. it is NOT our tagged pattern. */
    return (v & HEXA_NB_QNAN_MASK) != HEXA_NB_QNAN_MASK;
}

static inline uint32_t hexa_nb_tag(HexaV v) {
    return (uint32_t)((v & HEXA_NB_TAG_MASK) >> HEXA_NB_TAG_SHIFT);
}

static inline HexaNbKind hexa_nb_kind(HexaV v) {
    if (hexa_nb_is_double(v)) return HEXA_NB_KIND_FLOAT;
    switch (hexa_nb_tag(v)) {
        case HEXA_NB_TAG_NIL:     return HEXA_NB_KIND_NIL;
        case HEXA_NB_TAG_BOOL:    return HEXA_NB_KIND_BOOL;
        case HEXA_NB_TAG_INT32:   return HEXA_NB_KIND_INT;
        case HEXA_NB_TAG_CHAR:    return HEXA_NB_KIND_CHAR;
        case HEXA_NB_TAG_STR:     return HEXA_NB_KIND_STR;
        case HEXA_NB_TAG_ARRAY:   return HEXA_NB_KIND_ARRAY;
        case HEXA_NB_TAG_MAP:     return HEXA_NB_KIND_MAP;
        case HEXA_NB_TAG_STRUCT:  return HEXA_NB_KIND_STRUCT;
        case HEXA_NB_TAG_CLOSURE: return HEXA_NB_KIND_CLOSURE;
        case HEXA_NB_TAG_FN:      return HEXA_NB_KIND_FN;
        default:                  return HEXA_NB_KIND_NIL;  /* reserved → nil */
    }
}

/* ── Constructors ─────────────────────────────────────────── */
static inline HexaV hexa_nb_nil(void) {
    return HEXA_NB_QNAN_MASK | ((uint64_t)HEXA_NB_TAG_NIL << HEXA_NB_TAG_SHIFT);
}
static inline HexaV hexa_nb_bool(int b) {
    return HEXA_NB_QNAN_MASK
         | ((uint64_t)HEXA_NB_TAG_BOOL << HEXA_NB_TAG_SHIFT)
         | (uint64_t)(b ? 1 : 0);
}
static inline HexaV hexa_nb_int32(int32_t n) {
    /* Store as unsigned-cast 32-bit; sign-extend on read. */
    return HEXA_NB_QNAN_MASK
         | ((uint64_t)HEXA_NB_TAG_INT32 << HEXA_NB_TAG_SHIFT)
         | (uint64_t)(uint32_t)n;
}
static inline HexaV hexa_nb_char(uint32_t cp) {
    return HEXA_NB_QNAN_MASK
         | ((uint64_t)HEXA_NB_TAG_CHAR << HEXA_NB_TAG_SHIFT)
         | (uint64_t)(cp & 0x001FFFFFu);  /* 21 bits Unicode */
}
static inline HexaV hexa_nb_float(double f) {
    /* Canonicalize any NaN input to a single representation so we don't
     * accidentally create values that overlap with our tagged space. */
    uint64_t u = hexa_nb__d2u(f);
    if ((u & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL &&
        (u & 0x000FFFFFFFFFFFFFULL) != 0) {
        /* NaN → canonical quiet NaN (mantissa bit 51 set, no tag, no payload) */
        u = 0x7FF8000000000000ULL;
    }
    return u;
}
static inline HexaV hexa_nb_ptr(uint32_t tag, void* p) {
    /* Pointer must fit in 48 bits (canonical userspace on x86_64/arm64). */
    uint64_t pu = (uint64_t)(uintptr_t)p;
    return HEXA_NB_QNAN_MASK
         | ((uint64_t)tag << HEXA_NB_TAG_SHIFT)
         | (pu & HEXA_NB_PAYLOAD_MASK);
}

/* ── Accessors ────────────────────────────────────────────── */
static inline int64_t hexa_nb_as_int(HexaV v) {
    /* Sign-extend 32-bit payload to int64. */
    return (int64_t)(int32_t)(uint32_t)(v & 0xFFFFFFFFULL);
}
static inline double hexa_nb_as_float(HexaV v) {
    return hexa_nb__u2d(v);
}
static inline int hexa_nb_as_bool(HexaV v) {
    return (int)(v & 0x1);
}
static inline uint32_t hexa_nb_as_char(HexaV v) {
    return (uint32_t)(v & 0x001FFFFFu);
}
static inline void* hexa_nb_as_ptr(HexaV v) {
    /* Extract 48-bit payload. For arm64 TBI / x86_64 canonical pointers the
     * top 16 bits are sign-extension of bit 47; since all observed heap
     * pointers from malloc are in the low canonical half (bit 47 == 0), this
     * zero-extension is correct. If we ever target kernel-space pointers or
     * MAP_32BIT with weird layouts, sign-extend bit 47 here. */
    return (void*)(uintptr_t)(v & HEXA_NB_PAYLOAD_MASK);
}

/* Kind-specific type predicates (cheap: one mask + one compare) */
static inline int hexa_nb_is_nil(HexaV v)     { return !hexa_nb_is_double(v) && hexa_nb_tag(v) == HEXA_NB_TAG_NIL; }
static inline int hexa_nb_is_bool(HexaV v)    { return !hexa_nb_is_double(v) && hexa_nb_tag(v) == HEXA_NB_TAG_BOOL; }
static inline int hexa_nb_is_int(HexaV v)     { return !hexa_nb_is_double(v) && hexa_nb_tag(v) == HEXA_NB_TAG_INT32; }
static inline int hexa_nb_is_char(HexaV v)    { return !hexa_nb_is_double(v) && hexa_nb_tag(v) == HEXA_NB_TAG_CHAR; }
static inline int hexa_nb_is_str(HexaV v)     { return !hexa_nb_is_double(v) && hexa_nb_tag(v) == HEXA_NB_TAG_STR; }
static inline int hexa_nb_is_array(HexaV v)   { return !hexa_nb_is_double(v) && hexa_nb_tag(v) == HEXA_NB_TAG_ARRAY; }
static inline int hexa_nb_is_map(HexaV v)     { return !hexa_nb_is_double(v) && hexa_nb_tag(v) == HEXA_NB_TAG_MAP; }
static inline int hexa_nb_is_struct(HexaV v)  { return !hexa_nb_is_double(v) && hexa_nb_tag(v) == HEXA_NB_TAG_STRUCT; }
static inline int hexa_nb_is_closure(HexaV v) { return !hexa_nb_is_double(v) && hexa_nb_tag(v) == HEXA_NB_TAG_CLOSURE; }
static inline int hexa_nb_is_fn(HexaV v)      { return !hexa_nb_is_double(v) && hexa_nb_tag(v) == HEXA_NB_TAG_FN; }

#endif  /* HEXA_NANBOX_H */
