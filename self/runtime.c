// ═══════════════════════════════════════════════════════════
//  HEXA C Runtime Library — Phase 5 self-hosting support
//  Provides: tagged values, strings, arrays, maps, GC
// ═══════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include <dlfcn.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#if defined(__APPLE__)
#include <execinfo.h>
#endif

// Force line-buffered stdout when redirected to file/pipe.
__attribute__((constructor))
static void _hexa_init_stdio(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
}

// ═══════════════════════════════════════════════════════════
//  Optimization #11: String Interning
//  Hash-set of unique strings. Short strings (< 64 chars)
//  are interned; comparison becomes pointer equality (==).
//  Lazily initialized on first hexa_intern() call.
// ═══════════════════════════════════════════════════════════

#define INTERN_INIT_CAP   1024
#define INTERN_MAX_LEN    64
#define INTERN_LOAD_MAX   75   // percent

static uint32_t hexa_fnv1a(const char* s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

// Also used by the hash-map (#10)
static uint32_t hexa_fnv1a_str(const char* s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h ^= (uint8_t)*s;
        h *= 16777619u;
    }
    return h;
}

typedef struct {
    char** buckets;      // array of interned string pointers (NULL = empty)
    uint32_t* hashes;    // cached hash per slot
    int cap;             // power-of-2 capacity
    int count;           // number of occupied slots
} HexaInternTable;

static HexaInternTable __hexa_intern = {NULL, NULL, 0, 0};

static void hexa_intern_init(void) {
    __hexa_intern.cap = INTERN_INIT_CAP;
    __hexa_intern.count = 0;
    __hexa_intern.buckets = (char**)calloc(INTERN_INIT_CAP, sizeof(char*));
    __hexa_intern.hashes  = (uint32_t*)calloc(INTERN_INIT_CAP, sizeof(uint32_t));
}

static void hexa_intern_grow(void) {
    int old_cap = __hexa_intern.cap;
    char** old_buckets = __hexa_intern.buckets;
    uint32_t* old_hashes = __hexa_intern.hashes;
    int new_cap = old_cap * 2;
    char** new_buckets = (char**)calloc(new_cap, sizeof(char*));
    uint32_t* new_hashes = (uint32_t*)calloc(new_cap, sizeof(uint32_t));
    uint32_t mask = (uint32_t)(new_cap - 1);
    for (int i = 0; i < old_cap; i++) {
        if (old_buckets[i]) {
            uint32_t idx = old_hashes[i] & mask;
            while (new_buckets[idx]) idx = (idx + 1) & mask;
            new_buckets[idx] = old_buckets[i];
            new_hashes[idx] = old_hashes[i];
        }
    }
    free(old_buckets);
    free(old_hashes);
    __hexa_intern.buckets = new_buckets;
    __hexa_intern.hashes  = new_hashes;
    __hexa_intern.cap = new_cap;
}

// Intern a string: returns a canonical pointer.
// If the string is already interned, returns the existing pointer.
// Only interns strings shorter than INTERN_MAX_LEN.
// The returned pointer is owned by the intern table -- callers must NOT free it.
static const char* hexa_intern(const char* s) {
    if (!s) return s;
    size_t slen = strlen(s);
    if (slen >= INTERN_MAX_LEN) return NULL;  // too long, skip interning

    // Lazy init
    if (!__hexa_intern.buckets) hexa_intern_init();

    uint32_t h = hexa_fnv1a(s, slen);
    uint32_t mask = (uint32_t)(__hexa_intern.cap - 1);
    uint32_t idx = h & mask;

    while (__hexa_intern.buckets[idx]) {
        if (__hexa_intern.hashes[idx] == h &&
            strcmp(__hexa_intern.buckets[idx], s) == 0) {
            return __hexa_intern.buckets[idx];  // already interned
        }
        idx = (idx + 1) & mask;
    }

    // Not found -- insert
    if (__hexa_intern.count * 100 / __hexa_intern.cap >= INTERN_LOAD_MAX) {
        hexa_intern_grow();
        // Recompute slot after grow
        mask = (uint32_t)(__hexa_intern.cap - 1);
        idx = h & mask;
        while (__hexa_intern.buckets[idx]) idx = (idx + 1) & mask;
    }

    char* dup = strdup(s);
    __hexa_intern.buckets[idx] = dup;
    __hexa_intern.hashes[idx]  = h;
    __hexa_intern.count++;
    return dup;
}

// Forward declarations for all runtime functions
// S1-D3b-beta: struct tag renamed to HexaVal_ so typedef HexaVal can flip
// to uint64_t (NaN-boxed) without touching field access patterns.
typedef struct HexaVal_ HexaVal;
HexaVal hexa_add(HexaVal a, HexaVal b);
HexaVal hexa_concat_many(int n, HexaVal* parts);
int hexa_truthy(HexaVal v);
HexaVal hexa_sub(HexaVal a, HexaVal b);
HexaVal hexa_mul(HexaVal a, HexaVal b);
HexaVal hexa_div(HexaVal a, HexaVal b);
HexaVal hexa_mod(HexaVal a, HexaVal b);
HexaVal hexa_fma(HexaVal a, HexaVal b, HexaVal c);
HexaVal hexa_eq(HexaVal a, HexaVal b);
HexaVal hexa_cmp_lt(HexaVal a, HexaVal b);
HexaVal hexa_cmp_gt(HexaVal a, HexaVal b);
HexaVal hexa_cmp_le(HexaVal a, HexaVal b);
HexaVal hexa_cmp_ge(HexaVal a, HexaVal b);
HexaVal hexa_to_string(HexaVal v);
HexaVal hexa_str_concat(HexaVal a, HexaVal b);
HexaVal hexa_abs(HexaVal v);
HexaVal hexa_pad_left(HexaVal s, HexaVal width);
HexaVal hexa_pad_right(HexaVal s, HexaVal width);
HexaVal hexa_str_char_at(HexaVal s, HexaVal idx);
HexaVal hexa_str_char_code_at(HexaVal s, HexaVal idx);
HexaVal hexa_eprintln(HexaVal v);
HexaVal hexa_eprint(HexaVal v);
// M1 full · file layer (hxa-20260423-003 Step 5): codegen emits rt_file_*
// directly; hexa_file_* shims retired. rt_file_* are the SSOT (hand-port
// libc primitives — file I/O not meaningful to compose in .hexa).
HexaVal rt_read_file(HexaVal path);
HexaVal rt_write_file(HexaVal path, HexaVal content);
HexaVal rt_file_exists(HexaVal path);
HexaVal rt_write_bytes(HexaVal path, HexaVal arr);
HexaVal rt_write_bytes_v(HexaVal path, HexaVal arr);
HexaVal rt_read_file_bytes(HexaVal path);
HexaVal rt_read_bytes_at(HexaVal path, HexaVal offset, HexaVal nbytes);
HexaVal rt_write_bytes_append(HexaVal path, HexaVal arr);
HexaVal rt_write_bytes_append_v(HexaVal path, HexaVal arr);
HexaVal rt_file_size(HexaVal path);
HexaVal rt_read_lines(HexaVal path);
HexaVal rt_delete_file(HexaVal path);
HexaVal rt_append_file(HexaVal path, HexaVal content);
// M1 full · str_ext layer (hxa-20260423-003 Step 3/5): rt_str_* hand-port
// in runtime.c; hexa_str_* wrappers removed — codegen emits rt_str_*
// directly. ABI: rt_str_trim/trim_start/trim_end/to_upper/to_lower return
// HexaVal; rt_str_starts_with/ends_with return int (same as hexa_*).
HexaVal rt_str_trim(HexaVal s);
HexaVal rt_str_trim_start(HexaVal s);
HexaVal rt_str_trim_end(HexaVal s);
HexaVal rt_str_to_upper(HexaVal s);
HexaVal rt_str_to_lower(HexaVal s);
int rt_str_starts_with(HexaVal s, HexaVal prefix);
int rt_str_ends_with(HexaVal s, HexaVal suffix);
HexaVal hexa_str_split(HexaVal s, HexaVal delim);
HexaVal hexa_str_replace(HexaVal s, HexaVal old, HexaVal new_s);
HexaVal hexa_str_join(HexaVal arr, HexaVal sep);
HexaVal hexa_str_chars(HexaVal s);
HexaVal hexa_format_n(HexaVal fmt, HexaVal args);
HexaVal hexa_str_parse_int(HexaVal s);
HexaVal hexa_str_parse_float(HexaVal s);
HexaVal hexa_str_slice(HexaVal s, HexaVal start, HexaVal end);
HexaVal hexa_str_bytes(HexaVal s);
HexaVal hexa_array_slice(HexaVal arr, HexaVal start, HexaVal end);
HexaVal hexa_array_map(HexaVal arr, HexaVal fn);
HexaVal hexa_array_filter(HexaVal arr, HexaVal fn);
HexaVal hexa_array_fold(HexaVal arr, HexaVal init, HexaVal fn);
HexaVal hexa_array_index_of(HexaVal arr, HexaVal item);
HexaVal hexa_array_new(void);
HexaVal hexa_void(void);
HexaVal hexa_null_coal(HexaVal a, HexaVal b);
int hexa_is_type(HexaVal v, const char* type_name);
// rt 32-A: bulk struct constructor (see below for docstring).
HexaVal hexa_struct_pack_map(const char* type_name, int n,
                             const char* const* keys, const HexaVal* vals);
// rt 32-B: scratch-buffer primitives for reusable args vector (NK_CALL path).
HexaVal hexa_array_push_nostat(HexaVal arr, HexaVal item);
HexaVal hexa_val_snapshot_array(HexaVal v);  // rt#32-N forward decl
HexaVal hexa_array_shallow_clone(HexaVal v); // ROI #148 forward decl
HexaVal hexa_array_slice_fast(HexaVal arr, HexaVal start, HexaVal end);
HexaVal __hexa_range_array(HexaVal start, HexaVal end, int inclusive);
HexaVal hexa_matvec(HexaVal w, HexaVal x, HexaVal rows_v, HexaVal cols_v);
HexaVal hexa_math_abs(HexaVal x);
HexaVal hexa_math_sqrt(HexaVal x);
HexaVal hexa_math_floor(HexaVal x);
HexaVal hexa_math_ceil(HexaVal x);
HexaVal hexa_math_round(HexaVal x);
HexaVal hexa_math_pow(HexaVal b, HexaVal e);
HexaVal hexa_math_min(HexaVal a, HexaVal b);
HexaVal hexa_math_max(HexaVal a, HexaVal b);
// FIX-A (Anima serving unblock): time_ms / byte_len / dict_keys builtin C syms.
HexaVal hexa_time_ms(void);
HexaVal hexa_byte_len(HexaVal v);
HexaVal hexa_dict_keys(HexaVal m);
// FIX-A (Anima ML eval unblock): 8 tensor_* fallback builtins.
HexaVal hexa_tensor_zeros(HexaVal n);
HexaVal hexa_tensor_slice(HexaVal a, HexaVal lo, HexaVal hi);
HexaVal hexa_tensor_add(HexaVal a, HexaVal b);
HexaVal hexa_tensor_dot(HexaVal a, HexaVal b);
HexaVal hexa_tensor_mul_scalar(HexaVal a, HexaVal s);
HexaVal hexa_rms_norm(HexaVal x, HexaVal gamma, HexaVal eps);
HexaVal hexa_softmax(HexaVal a);
HexaVal hexa_matmul(HexaVal a, HexaVal b, HexaVal m, HexaVal k, HexaVal n);
// FIX-A 2nd batch (2026-04-19): tensor_ones + swiglu_vec for anima serving/training.
HexaVal hexa_tensor_ones(HexaVal n);
HexaVal hexa_swiglu_vec(HexaVal gate, HexaVal up);

// RT-P3-1 wrapper shims — tagged-value → C-native conversion for codegen regen
// path. Close Wint-conversion / Wpointer-arith categories in the
// interpreter.hexa → .c baseline. See self/runtime_hexaval_DESIGN.md
// §Production runtime (live).
int64_t     hexa_as_num(HexaVal v);
const char* hexa_to_cstring(HexaVal v);
const char* hexa_str_as_ptr(HexaVal v);

// rt 32-G Phase 0: flat Val struct forward decls (implementation below).
// NOTE: parameter-less typedef stays outside so the struct body later in
// this file keeps using the same name. Function decls reference HexaVal
// (defined below) and the fully-defined HexaValStruct — OK because the C
// compiler only needs full types at call sites, not at forward decls.
typedef struct HexaValStruct HexaValStruct;
// Redesign: only the _v variant is exposed. All non-scalar arguments are
// passed as HexaVal so polymorphic payloads (closures, AST bodies, struct
// field maps) survive verbatim instead of being coerced to "".
HexaVal hexa_valstruct_new_v(HexaVal, HexaVal, HexaVal, HexaVal, HexaVal,
    HexaVal, HexaVal, HexaVal, HexaVal, HexaVal, HexaVal, HexaVal);
HexaVal hexa_valstruct_int(HexaVal v);
HexaVal hexa_valstruct_get_by_key(HexaVal v, const char* key);
HexaVal hexa_valstruct_set_by_key(HexaVal v, const char* key, HexaVal val);

// rt#37: inline cache for obj.field — the struct is defined here so every
// generated C file (via #include "runtime.c") can declare `static HexaIC`
// slots. The lookup itself is defined below as `static inline`.
typedef struct HexaIC {
    void* keys_ptr;   // last-seen m.map.keys pointer (shape id)
    int   len;        // last-seen m.map.len
    int   idx;        // cached field offset
    uint64_t hits;
    uint64_t misses;
} HexaIC;

// ── Tagged Value ─────────────────────────────────────────
// All HEXA values are represented as tagged unions (NaN-boxing alternative)

typedef enum {
    TAG_INT = 0, TAG_FLOAT, TAG_BOOL, TAG_STR, TAG_VOID,
    TAG_ARRAY, TAG_MAP, TAG_FN, TAG_CHAR, TAG_CLOSURE,
    // rt 32-G Phase 0: flat C struct replacement for interpreter `Val` map.
    // Val is the interpreter's tagged value carrier — ~3.37M constructions
    // per d64 200-step run. Each map-backed Val = 1 map_new + 12 map_set =
    // 13 hash-table insertions. Replacing with a flat 12-field C struct
    // collapses that to a single heap alloc, eliminating 7GB+ RSS pressure.
    // Fields mirror `struct Val` in self/hexa_full.hexa exactly (12 fields).
    TAG_VALSTRUCT
} HexaTag;

// ── Optimization #10: Hash-map backing store ─────────────
// Open-addressing hash table (Robin Hood not needed at this scale).
// Stored on the heap; the HexaVal map union holds a pointer + len.
// Keeps a parallel insertion-order array for keys()/values()/iter.

#define HMAP_INIT_CAP   16
#define HMAP_LOAD_MAX   75  // percent

typedef struct {
    char*  key;       // owned string (strdup'd) -- NULL means empty slot
    uint32_t hash;    // cached FNV-1a of key
    int order_idx;    // ROI-24: index into order_keys/order_vals arrays — O(1) update
} HexaMapSlot;

typedef struct {
    HexaMapSlot* slots;   // hash table (power-of-2 sized)
    HexaVal* vals;  // parallel values array (same indices as slots)
    int ht_cap;            // hash table capacity (power-of-2)

    // Insertion-order arrays for keys()/values()/for-in iteration
    char** order_keys;     // ordered key pointers (point into slots[].key)
    HexaVal* order_vals; // ordered values
    int len;               // number of entries
    int order_cap;         // allocated capacity for order arrays
    // rt 32-L: Val-arena support. When non-zero, this table + its slot/val/
    // order arrays were allocated from the bump arena (NOT malloc). Such tables
    // MUST be heapified before they outlive the current scope; otherwise an
    // arena rewind invalidates the backing storage. Slots' .key strings are
    // ALWAYS strdup'd (heap) regardless of from_arena, because the arena is
    // bulk-rewound and per-string lifetime tracking is impractical here.
    int from_arena;
} HexaMapTable;

// ── S1-D3a: Heap descriptor structs for compound types ───
// NaN-boxing prep: inline union members → heap-allocated descriptors.
// sizeof(HexaVal) unchanged (largest union member is still a pointer).
typedef struct HexaArr {
    HexaVal* items;
    int len;
    int cap;
} HexaArr;

typedef struct HexaMap {
    HexaMapTable* tbl;
    int len;
} HexaMap;

typedef struct HexaFn {
    void* fn_ptr;
    int arity;
} HexaFn;

typedef struct HexaClo {
    void* fn_ptr;
    int arity;
    HexaVal* env_box;
} HexaClo;

// HexaVal must be defined before HexaValStruct so that HexaValStruct's
// HexaVal-by-value fields (rt 32-G redesign) have a complete type. HexaVal
// only needs HexaValStruct as a *pointer* (.vs), so the forward decl above
// (typedef struct HexaValStruct HexaValStruct) is sufficient here.
typedef struct HexaVal_ {
    HexaTag tag;
    union {
        int64_t i;
        double f;
        int b;
        char* s;           // owned string (malloc'd)
        HexaArr* arr_ptr;  // S1-D3a: heap descriptor (was inline struct)
        HexaMap* map_ptr;  // S1-D3a: heap descriptor (was inline struct)
        HexaFn*  fn_ptr_d; // S1-D3a: heap descriptor (was inline struct)
        HexaClo* clo_ptr;  // S1-D3a: heap descriptor (was inline struct)
        // rt 32-G: heap-allocated flat struct pointer (TAG_VALSTRUCT).
        HexaValStruct* vs;
    };
} HexaVal;

// ═══════════════════════════════════════════════════════════
//  STRUCTURAL-1 Phase A: NaN-boxing accessor macros
//  Current: identity-map to tagged union fields (zero-cost).
//  Phase B: macro bodies swap to NaN-boxed 8B representation.
//  All hot-path code MUST use these instead of raw .tag/.i/.f etc.
// ═══════════════════════════════════════════════════════════

#define HX_TAG(v)       ((v).tag)
#define HX_INT(v)       ((v).i)
#define HX_INT_U(v)     ((uint64_t)(v).i)   // unsigned read — safe for NaN-boxed pointers
#define HX_FLOAT(v)     ((v).f)
#define HX_BOOL(v)      ((v).b)
#define HX_STR(v)       ((v).s)
// S1-D3a: compound-type READ macros — dereference heap descriptors.
// IMPORTANT: caller MUST ensure the tag matches before calling these;
// they dereference the descriptor pointer unconditionally for performance.
// The one exception is HX_MAP_TBL which is guarded because the IC
// fast-path (hexa_map_get_ic) probes it before a tag check, and
// hexa_map_get probes it on TAG_VALSTRUCT-routed values.
#define HX_ARR_LEN(v)   ((v).arr_ptr->len)
#define HX_ARR_ITEMS(v) ((v).arr_ptr->items)
#define HX_ARR_CAP(v)   ((v).arr_ptr->cap)
#define HX_MAP_LEN(v)   ((v).map_ptr->len)
#define HX_MAP_TBL(v)   (HX_IS_MAP(v) ? (v).map_ptr->tbl : (HexaMapTable*)0)
#define HX_FN_PTR(v)    ((v).fn_ptr_d->fn_ptr)
#define HX_FN_ARITY(v)  ((v).fn_ptr_d->arity)
#define HX_CLO_PTR(v)   ((v).clo_ptr->fn_ptr)
#define HX_CLO_ARITY(v) ((v).clo_ptr->arity)
#define HX_CLO_ENV(v)   ((v).clo_ptr->env_box)
#define HX_VS(v)        ((v).vs)

// ── S1-D3a: compound-type SET macros (write accessors) — heap descriptor ──
#define HX_SET_ARR_ITEMS(v, p) ((v).arr_ptr->items = (p))
#define HX_SET_ARR_LEN(v, n)   ((v).arr_ptr->len = (n))
#define HX_SET_ARR_CAP(v, n)   ((v).arr_ptr->cap = (n))
#define HX_SET_MAP_TBL(v, t)   ((v).map_ptr->tbl = (t))
#define HX_SET_MAP_LEN(v, n)   ((v).map_ptr->len = (n))
#define HX_SET_CLO_PTR(v, p)   ((v).clo_ptr->fn_ptr = (p))
#define HX_SET_CLO_ARITY(v, n) ((v).clo_ptr->arity = (n))
#define HX_SET_CLO_ENV(v, p)   ((v).clo_ptr->env_box = (p))

// ── STRUCTURAL-1 Phase A: scalar + descriptor-pointer writer macros ──
// Prep for NaN-boxing typedef flip (HexaVal → uint64_t). Once callers route
// all writes through these macros, the macro bodies swap to encode-into-u64
// in one place. Struct-initializer literals ({.tag=T, .i=n}) are OUT OF SCOPE
// here — they need a different rewrite (constructor fns or the flip itself).
#define HX_SET_TAG(v, t)       ((v).tag = (t))
#define HX_SET_INT(v, n)       ((v).i = (n))
#define HX_SET_FLOAT(v, f)     ((v).f = (f))
#define HX_SET_BOOL(v, b)      ((v).b = (b))
#define HX_SET_STR(v, p)       ((v).s = (p))
#define HX_SET_ARR_PTR(v, p)   ((v).arr_ptr = (p))
#define HX_SET_MAP_PTR(v, p)   ((v).map_ptr = (p))
#define HX_SET_FN_PTR_D(v, p)  ((v).fn_ptr_d = (p))
#define HX_SET_CLO_PTR_D(v, p) ((v).clo_ptr = (p))
#define HX_SET_VS(v, p)        ((v).vs = (p))

// HexaValStruct (inner) field access via v — routes through HX_VS so the
// typedef flip can adjust the .vs bits in one spot. The inner HexaValStruct
// remains a plain C struct — HX_VSF is pointer deref + member access.
#define HX_VSF(v, field)       (HX_VS(v)->field)

#define HX_IS_INT(v)    ((v).tag == TAG_INT)
#define HX_IS_FLOAT(v)  ((v).tag == TAG_FLOAT)
#define HX_IS_BOOL(v)   ((v).tag == TAG_BOOL)
#define HX_IS_STR(v)    ((v).tag == TAG_STR)
#define HX_IS_VOID(v)   ((v).tag == TAG_VOID)
#define HX_IS_ARRAY(v)  ((v).tag == TAG_ARRAY)
#define HX_IS_MAP(v)    ((v).tag == TAG_MAP)
#define HX_IS_FN(v)     ((v).tag == TAG_FN)
#define HX_IS_CLOSURE(v)((v).tag == TAG_CLOSURE)
#define HX_IS_VALSTRUCT(v) ((v).tag == TAG_VALSTRUCT)

// rt 32-G Phase 0 (redesigned): flat C struct replacement for `Val` map.
// Fields MUST mirror self/hexa_full.hexa `struct Val` exactly (order + types).
// Heap-allocated once per Val construction, shared by value via pointer copy
// of the containing HexaVal (no deep copy — mirrors current map.tbl pointer
// sharing semantics). Lifetime: leak-compatible with current interpreter
// (no free, same as existing map-backed Vals).
//
// REDESIGN (rt 32-G Phase 0 redesign): non-scalar fields are now full HexaVal
// (polymorphic tagged union) instead of `const char*`. The original Phase 0
// design assumed every non-scalar slot held a string, but val_fn / val_closure
// store TAG_ARRAY (params) and TAG_MAP (AST body) values. The old design
// silently coerced those to "" → closure body erased after rebootstrap.
// HexaVal-by-value adds ~24B per slot but eliminates the data loss.
typedef struct HexaValStruct {
    int64_t tag_i;          // Hexa-level Val.tag (TAG_INT / TAG_FLOAT / ...)
    int64_t int_val;
    double  float_val;
    int     bool_val;
    // rt 32-L: 1 = allocated from the bump arena, 0 = malloc heap. Arena
    // VALSTRUCTs MUST be heapified before they outlive the current scope.
    // Sits in the alignment padding between bool_val (4B) and the first
    // HexaVal (24B aligned to 8) — zero size cost in practice.
    int     from_arena;
    // Polymorphic slots — store the original HexaVal verbatim. For TAG_STR
    // payloads this is just s/tag; for arrays/maps the .arr / .map / .vs
    // member survives intact. Empty/missing fields use TAG_VOID.
    HexaVal str_val;
    HexaVal char_val;
    HexaVal array_val;     // crucial for AST body propagation
    HexaVal fn_name;
    HexaVal fn_params;     // TAG_ARRAY (closure param list)
    HexaVal fn_body;       // TAG_MAP / TAG_ARRAY (AST body)
    HexaVal struct_name;
    HexaVal struct_fields; // TAG_MAP (struct field map)
} HexaValStruct;

// ── C3 Closure helpers ───────────────────────────────────
// Build a closure value. Captured values are provided as an already-built
// TAG_ARRAY HexaVal; we heap-box it so the closure remains valid after copies.
static inline HexaVal hexa_closure_new(void* fn_ptr, int arity, HexaVal env_arr) {
    HexaVal v = {.tag=TAG_CLOSURE};
    HX_SET_CLO_PTR_D(v, (HexaClo*)calloc(1, sizeof(HexaClo)));
    HX_SET_CLO_PTR(v, fn_ptr);
    HX_SET_CLO_ARITY(v, arity);
    HX_SET_CLO_ENV(v, (HexaVal*)malloc(sizeof(HexaVal)));
    *HX_CLO_ENV(v) = env_arr;
    return v;
}

// S1-D2: NaN-boxing-safe constructor for TAG_FN values.
// S1-D3a: allocates HexaFn heap descriptor (was inline .fn={...} struct literal).
static inline HexaVal hexa_fn_new(void* fn_ptr, int arity) {
    HexaVal v = {.tag=TAG_FN};
    HX_SET_FN_PTR_D(v, (HexaFn*)calloc(1, sizeof(HexaFn)));
    HX_FN_PTR(v) = fn_ptr;
    HX_FN_ARITY(v) = arity;
    return v;
}

static inline HexaVal hexa_closure_env(HexaVal c) {
    if (!HX_IS_CLOSURE(c) || !HX_CLO_ENV(c)) return hexa_array_new();
    return *HX_CLO_ENV(c);
}

// Dispatched call helpers — one per arity we support (0..4).
static inline HexaVal hexa_call0(HexaVal f) {
    if (HX_IS_CLOSURE(f)) {
        HexaVal (*fp)(HexaVal) = (HexaVal(*)(HexaVal))HX_CLO_PTR(f);
        return fp(hexa_closure_env(f));
    }
    if (HX_IS_FN(f)) {
        HexaVal (*fp)(void) = (HexaVal(*)(void))HX_FN_PTR(f);
        return fp();
    }
    return hexa_void();
}
static inline HexaVal hexa_call1(HexaVal f, HexaVal a1) {
    if (HX_IS_CLOSURE(f)) {
        HexaVal (*fp)(HexaVal, HexaVal) = (HexaVal(*)(HexaVal, HexaVal))HX_CLO_PTR(f);
        return fp(hexa_closure_env(f), a1);
    }
    if (HX_IS_FN(f)) {
        HexaVal (*fp)(HexaVal) = (HexaVal(*)(HexaVal))HX_FN_PTR(f);
        return fp(a1);
    }
    return hexa_void();
}
static inline HexaVal hexa_call2(HexaVal f, HexaVal a1, HexaVal a2) {
    if (HX_IS_CLOSURE(f)) {
        HexaVal (*fp)(HexaVal, HexaVal, HexaVal) = (HexaVal(*)(HexaVal, HexaVal, HexaVal))HX_CLO_PTR(f);
        return fp(hexa_closure_env(f), a1, a2);
    }
    if (HX_IS_FN(f)) {
        HexaVal (*fp)(HexaVal, HexaVal) = (HexaVal(*)(HexaVal, HexaVal))HX_FN_PTR(f);
        return fp(a1, a2);
    }
    return hexa_void();
}
static inline HexaVal hexa_call3(HexaVal f, HexaVal a1, HexaVal a2, HexaVal a3) {
    if (HX_IS_CLOSURE(f)) {
        HexaVal (*fp)(HexaVal, HexaVal, HexaVal, HexaVal) = (HexaVal(*)(HexaVal, HexaVal, HexaVal, HexaVal))HX_CLO_PTR(f);
        return fp(hexa_closure_env(f), a1, a2, a3);
    }
    if (HX_IS_FN(f)) {
        HexaVal (*fp)(HexaVal, HexaVal, HexaVal) = (HexaVal(*)(HexaVal, HexaVal, HexaVal))HX_FN_PTR(f);
        return fp(a1, a2, a3);
    }
    return hexa_void();
}
static inline HexaVal hexa_call4(HexaVal f, HexaVal a1, HexaVal a2, HexaVal a3, HexaVal a4) {
    if (HX_IS_CLOSURE(f)) {
        HexaVal (*fp)(HexaVal, HexaVal, HexaVal, HexaVal, HexaVal) = (HexaVal(*)(HexaVal, HexaVal, HexaVal, HexaVal, HexaVal))HX_CLO_PTR(f);
        return fp(hexa_closure_env(f), a1, a2, a3, a4);
    }
    if (HX_IS_FN(f)) {
        HexaVal (*fp)(HexaVal, HexaVal, HexaVal, HexaVal) = (HexaVal(*)(HexaVal, HexaVal, HexaVal, HexaVal))HX_FN_PTR(f);
        return fp(a1, a2, a3, a4);
    }
    return hexa_void();
}

// ── Constructors ─────────────────────────────────────────

HexaVal hexa_int(int64_t n) { return (HexaVal){.tag=TAG_INT, .i=n}; }
HexaVal hexa_float(double f) { return (HexaVal){.tag=TAG_FLOAT, .f=f}; }
HexaVal hexa_bool(int b) { return (HexaVal){.tag=TAG_BOOL, .b=b}; }
HexaVal hexa_void() { return (HexaVal){.tag=TAG_VOID}; }

// T32: unwrap a HexaVal (possibly VALSTRUCT-wrapped by the interpreter) into
// a raw C double. The prior inline `v.tag==TAG_FLOAT?v.f:(double)v.i` read
// only the outer tag, so interpreter values (which carry TAG_VALSTRUCT with
// the real tag in vs->tag_i) fell through and cast the vs pointer bits to
// double — producing garbage. Centralising here lets to_float / exp / sin /
// cos / log / log10 / round share one correct unwrap.
static inline double __hx_to_double(HexaVal v) {
    if (HX_IS_FLOAT(v)) return HX_FLOAT(v);
    if (HX_IS_INT(v))   return (double)HX_INT(v);
    // ComptimeConst eval: to_float("3.14") at compile time needs string parsing.
    // Without this branch, returned 0.0, silently producing wrong constants.
    // atof handles both ints and floats correctly ("3" → 3.0, "3.14" → 3.14).
    if (HX_IS_STR(v) && HX_STR(v))   return atof(HX_STR(v));
    if (HX_IS_VALSTRUCT(v) && HX_VS(v)) {
        if (HX_VSF(v, tag_i) == TAG_FLOAT) return HX_VSF(v, float_val);
        if (HX_VSF(v, tag_i) == TAG_INT)   return (double)HX_VSF(v, int_val);
    }
    return 0.0;
}

// RT-P3-1 wrapper shims. Non-owning returns; cstring/str_as_ptr use static
// thread-unsafe buffers for scalar formatting — callers that persist the
// pointer beyond the next scalar-path call MUST copy.
int64_t hexa_as_num(HexaVal v) {
    if (HX_IS_INT(v))   return HX_INT(v);
    if (HX_IS_FLOAT(v)) return (int64_t)HX_FLOAT(v);
    if (HX_IS_BOOL(v))  return (int64_t)HX_BOOL(v);
    // P39 fix: auto-detect 0x/0X prefix in string coercion
    if (HX_IS_STR(v) && HX_STR(v)) {
        const char* cs = HX_STR(v); const char* p = cs;
        if (*p == '+' || *p == '-') p++;
        int base = (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) ? 16 : 10;
        return strtoll(cs, NULL, base);
    }
    if (HX_IS_VALSTRUCT(v) && HX_VS(v)) {
        if (HX_VSF(v, tag_i) == TAG_INT)   return HX_VSF(v, int_val);
        if (HX_VSF(v, tag_i) == TAG_FLOAT) return (int64_t)HX_VSF(v, float_val);
        if (HX_VSF(v, tag_i) == TAG_BOOL)  return (int64_t)HX_VSF(v, bool_val);
        if (HX_VSF(v, tag_i) == TAG_STR && HX_STR(HX_VSF(v, str_val))) {
            const char* cs = HX_STR(HX_VSF(v, str_val)); const char* p = cs;
            if (*p == '+' || *p == '-') p++;
            int base = (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) ? 16 : 10;
            return strtoll(cs, NULL, base);
        }
    }
    return 0;
}

const char* hexa_to_cstring(HexaVal v) {
    if (HX_IS_STR(v) && HX_STR(v)) return HX_STR(v);
    if (HX_IS_VOID(v)) return "void";
    if (HX_IS_VALSTRUCT(v) && HX_VS(v)) {
        if (HX_VSF(v, tag_i) == TAG_STR && HX_STR(HX_VSF(v, str_val))) return HX_STR(HX_VSF(v, str_val));
        static char vbuf[64];
        if (HX_VSF(v, tag_i) == TAG_INT)   { snprintf(vbuf, 64, "%lld", (long long)HX_VSF(v, int_val)); return vbuf; }
        if (HX_VSF(v, tag_i) == TAG_FLOAT) { snprintf(vbuf, 64, "%g", HX_VSF(v, float_val)); return vbuf; }
        if (HX_VSF(v, tag_i) == TAG_BOOL)  return HX_VSF(v, bool_val) ? "true" : "false";
    }
    static char buf[64];
    if (HX_IS_INT(v))   { snprintf(buf, 64, "%lld", (long long)HX_INT(v)); return buf; }
    if (HX_IS_FLOAT(v)) { snprintf(buf, 64, "%g", HX_FLOAT(v)); return buf; }
    if (HX_IS_BOOL(v))  return HX_BOOL(v) ? "true" : "false";
    // 2026-04-20 silent-fallback audit: prior code returned the literal
    // "<value>" for TAG_ARRAY / TAG_MAP / TAG_FN / TAG_CHAR / TAG_CLOSURE.
    // Callers include codegen-emitted `map.has/get/set(k)` paths and FFI
    // entry points (net.c) — a literal "<value>" string silently collides
    // across every bad callsite (all array/map/fn keys hash to the same
    // bucket). Delegate to hexa_to_string for a structural repr that at
    // least preserves distinctness ("[1, 2]" != "[3, 4]") and surface the
    // root cause in logs rather than corrupting the map. The returned
    // pointer points into the intern table (hexa_to_string → hexa_str),
    // so lifetime matches the legacy static-buf contract for callers that
    // treat the result as short-lived.
    HexaVal s = hexa_to_string(v);
    if (HX_IS_STR(s) && HX_STR(s)) return HX_STR(s);
    return "<value>";
}

const char* hexa_str_as_ptr(HexaVal v) {
    if (HX_IS_STR(v) && HX_STR(v)) return HX_STR(v);
    if (HX_IS_VALSTRUCT(v) && HX_VS(v) && HX_VSF(v, tag_i) == TAG_STR && HX_STR(HX_VSF(v, str_val)))
        return HX_STR(HX_VSF(v, str_val));
    return NULL;
}

// Null coalescing: a ?? b — if a is void or empty string, return b
HexaVal hexa_null_coal(HexaVal a, HexaVal b) {
    if (HX_IS_VOID(a)) return b;
    if (HX_IS_STR(a) && (HX_STR(a) == NULL || HX_STR(a)[0] == '\0')) return b;
    return a;
}

HexaVal hexa_str(const char* s) {
    HexaVal v = {.tag=TAG_STR};
    // Optimization #11: intern short strings for pointer-equality comparison
    const char* interned = hexa_intern(s);
    if (interned) {
        HX_SET_STR(v, (char*)interned);  // owned by intern table, not caller
    } else {
        HX_SET_STR(v, strdup(s));        // long/unique strings: traditional copy
    }
    return v;
}

HexaVal hexa_str_own(char* s) {
    return (HexaVal){.tag=TAG_STR, .s=s};
}

// ═══════════════════════════════════════════════════════════
//  ROI-33: Pre-interned string cache
//  "true","false","void" (+ type_of strings) are used on every
//  hexa_to_string / hexa_type_of call.  Cache them as static
//  HexaVal so we skip strlen+hash+probe entirely.
//  Lazy-init on first access (no constructor needed).
// ═══════════════════════════════════════════════════════════

static HexaVal _cached_str_true;
static HexaVal _cached_str_false;
static HexaVal _cached_str_void;
static HexaVal _cached_str_int;
static HexaVal _cached_str_float;
static HexaVal _cached_str_bool;
static HexaVal _cached_str_string;
static HexaVal _cached_str_array;
static HexaVal _cached_str_map;
static HexaVal _cached_str_struct;
static HexaVal _cached_str_fn;
static HexaVal _cached_str_char;
static HexaVal _cached_str_closure;
static HexaVal _cached_str_unknown;
static HexaVal _cached_str_value;  // "<value>" fallback
static int     _cached_strs_ready = 0;
// S1-D2 Blocker C: forward decl — defined after bt73 wrappers near EOF.
static void _hexa_init_fn_shims(void);

static void _hexa_init_cached_strs(void) {
    if (_cached_strs_ready) return;
    _cached_str_true    = hexa_str("true");
    _cached_str_false   = hexa_str("false");
    _cached_str_void    = hexa_str("void");
    _cached_str_int     = hexa_str("int");
    _cached_str_float   = hexa_str("float");
    _cached_str_bool    = hexa_str("bool");
    _cached_str_string  = hexa_str("string");
    _cached_str_array   = hexa_str("array");
    _cached_str_map     = hexa_str("map");
    _cached_str_struct  = hexa_str("struct");
    _cached_str_fn      = hexa_str("fn");
    _cached_str_char    = hexa_str("char");
    _cached_str_closure = hexa_str("closure");
    _cached_str_unknown = hexa_str("unknown");
    _cached_str_value   = hexa_str("<value>");
    _cached_strs_ready  = 1;
}

// Small-int to_string cache: [-1, 0..255] covers >90% of int-to-string
#define SMALL_INT_CACHE_MIN  (-1)
#define SMALL_INT_CACHE_MAX  255
#define SMALL_INT_CACHE_SIZE (SMALL_INT_CACHE_MAX - SMALL_INT_CACHE_MIN + 1)
static HexaVal _cached_small_ints[SMALL_INT_CACHE_SIZE];
static int     _cached_small_ints_ready = 0;

static void _hexa_init_small_int_cache(void) {
    if (_cached_small_ints_ready) return;
    char buf[16];
    for (int i = 0; i < SMALL_INT_CACHE_SIZE; i++) {
        int64_t n = (int64_t)(i + SMALL_INT_CACHE_MIN);
        snprintf(buf, 16, "%lld", (long long)n);
        _cached_small_ints[i] = hexa_str(buf);
    }
    _cached_small_ints_ready = 1;
}

// ── Array operations ─────────────────────────────────────

// rt 32: alloc/COW stats (HEXA_ALLOC_STATS=1 env enables atexit dump).
// Counts hot-path C runtime allocations so we can diagnose where 10GB
// RSS ML microbench pressure originates without sample/leaks tooling.
static int64_t _hx_stats_array_new      = 0;
static int64_t _hx_stats_array_push     = 0;
static int64_t _hx_stats_array_grow     = 0;   // realloc that actually grows cap
static int64_t _hx_stats_str_concat     = 0;
static int64_t _hx_stats_array_reserve  = 0;
static int64_t _hx_stats_map_new        = 0;
static int64_t _hx_stats_map_set        = 0;
// STRUCTURAL-7 Phase A: codegen_c2 emits __hexa_fn_arena_enter/return for
// per-function arena scoping. Forward declarations here; real implementations
// below after all arena dependencies (hexa_val_arena_on, hexa_val_heapify,
// hexa_val_arena_scope_push/pop) are defined.
void __hexa_fn_arena_enter(void);
HexaVal __hexa_fn_arena_return(HexaVal v);

// rt 32-D: arena allocator stats (str_concat hot-path)
static int64_t _hx_stats_arena_alloc    = 0;   // total arena bump allocations
static int64_t _hx_stats_arena_blocks   = 0;   // blocks created
static int64_t _hx_stats_arena_bytes    = 0;   // bytes currently reserved in blocks
static int64_t _hx_stats_str_concat_arena = 0; // str_concat calls that hit arena
// rt 32-M: array arena counters
static int64_t _hx_stats_array_arena_alloc   = 0; // push that went arena (initial slot buf)
static int64_t _hx_stats_array_arena_promote = 0; // arena→heap promotions on grow
static int64_t _hx_stats_array_arena_heapify = 0; // heapify copies of arena item buffers
// M4: string arena heapify stats
static int64_t _hx_stats_str_arena_heapify   = 0; // arena→heap string promotions
// M4-edge: RSS peak sampler — opt-in via HEXA_ALLOC_STATS. Sampled only on
// arena new-block (rare: every 4MB+), so getrusage overhead is negligible.
static int64_t _hx_stats_rss_peak_bytes      = 0;
static int _hx_stats_enabled = -1;             // lazy probe of env

static void _hx_stats_sample_rss(void) {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return;
#if defined(__APPLE__)
    int64_t rss_bytes = (int64_t)ru.ru_maxrss;        // macOS: bytes
#else
    int64_t rss_bytes = (int64_t)ru.ru_maxrss * 1024; // Linux: kilobytes
#endif
    if (rss_bytes > _hx_stats_rss_peak_bytes) _hx_stats_rss_peak_bytes = rss_bytes;
}

static void _hx_stats_dump(void) {
    _hx_stats_sample_rss();
    fprintf(stderr, "[HEXA_ALLOC_STATS] array_new=%lld push=%lld grow=%lld reserve=%lld str_concat=%lld map_new=%lld map_set=%lld arena_alloc=%lld arena_blocks=%lld arena_bytes=%lld str_concat_arena=%lld arr_arena_alloc=%lld arr_arena_promote=%lld arr_arena_heapify=%lld str_arena_heapify=%lld rss_peak_mb=%lld\n",
        (long long)_hx_stats_array_new,
        (long long)_hx_stats_array_push,
        (long long)_hx_stats_array_grow,
        (long long)_hx_stats_array_reserve,
        (long long)_hx_stats_str_concat,
        (long long)_hx_stats_map_new,
        (long long)_hx_stats_map_set,
        (long long)_hx_stats_arena_alloc,
        (long long)_hx_stats_arena_blocks,
        (long long)_hx_stats_arena_bytes,
        (long long)_hx_stats_str_concat_arena,
        (long long)_hx_stats_array_arena_alloc,
        (long long)_hx_stats_array_arena_promote,
        (long long)_hx_stats_array_arena_heapify,
        (long long)_hx_stats_str_arena_heapify,
        (long long)(_hx_stats_rss_peak_bytes / (1024 * 1024)));
}

static int _hx_stats_on(void) {
    if (_hx_stats_enabled < 0) {
        const char* e = getenv("HEXA_ALLOC_STATS");
        _hx_stats_enabled = (e && e[0] && e[0] != '0') ? 1 : 0;
        if (_hx_stats_enabled) {
            atexit(_hx_stats_dump);
        }
    }
    return _hx_stats_enabled;
}

// ── rt 32-M: Array arena allocator ──────────────────────
// Bump-allocate the array slot buffer (HexaVal*) from the shared bump arena
// during an active fn-call mark (__hexa_val_mark_top > 0). On grow beyond the
// initial slot capacity, promote the buffer to malloc (can't realloc an arena
// region). On heapify, deep-copy the item buffer to malloc before the arena
// rewind point.
//
// Encoding: negative cap = from_arena (arr.items points into arena). This
// preserves HexaVal width/ABI — no new field in the .arr struct. Consumers
// that read .cap positively must treat (cap < 0) as |cap|.
//
// Gate: HEXA_ARRAY_ARENA env var. Default ON. Set =0 to fall back to pure
// malloc/realloc path bit-for-bit.
//
// Rationale: fib(20)×30 shows array_new=4.6M, push=28.9M, grow=4.6M — the
// dominant RSS driver. rt 32-L heapified HexaValStruct but left the array
// .items buffer on the malloc heap (rt 32-L comment explicitly calls this
// out: "Array .items lives on heap — hexa_array_push uses realloc"). This
// change addresses that gap.
extern int __hexa_val_mark_top;  // shared with rt 32-L Val arena
extern int __hexa_val_force_heap; // T33-fix-2: force heap in valstruct_new_v
static int hexa_val_arena_on(void);
static void* hexa_val_arena_calloc(size_t n);
void* hexa_arena_alloc(size_t n);  // rt 32-D bump allocator (shared block chain)

static int __hexa_array_arena_enabled = -1;
static int hexa_array_arena_on(void) {
    if (__hexa_array_arena_enabled < 0) {
        const char* e = getenv("HEXA_ARRAY_ARENA");
        // Default ON. Export HEXA_ARRAY_ARENA=0 to disable.
        if (!e || !e[0]) __hexa_array_arena_enabled = 1;
        else             __hexa_array_arena_enabled = (e[0] != '0') ? 1 : 0;
    }
    return __hexa_array_arena_enabled;
}

// Arena-alloc a HexaVal slot buffer. Caller sets cap to -n so the "from_arena"
// sentinel survives the return. Returns NULL if arena is off / not in scope.
// Does NOT zero the buffer — caller memcpy's over it (slice_fast) or writes
// len items before reading. Skipping the memset is a measurable win on hot
// paths (the Val arena's calloc variant memset'd a 4MB block front-to-back
// every few thousand calls, costing ~8% wall on fib K=30).
static HexaVal* hexa_array_arena_alloc_items(int n) {
    if (!hexa_array_arena_on()) return NULL;
    // Must be inside a live scope mark — otherwise there's no rewind point to
    // catch the allocation (module-init arrays stay on heap).
    if (__hexa_val_mark_top <= 0) return NULL;
    // Reuse the shared bump allocator (same block chain as rt 32-L). The
    // existing rt 32-L scope-push/pop discipline automatically reclaims the
    // array buffers too — no parallel mark stack needed.
    HexaVal* p = (HexaVal*)hexa_arena_alloc((size_t)n * sizeof(HexaVal));
    return p;  // NULL if arena OOM — caller falls back to malloc
}

// Promote an arena-backed slot buffer to malloc. len = current live slot count
// to copy; new_cap = target capacity. Used by grow (new_cap > |old_cap|) and
// heapify (new_cap == |old_cap|). Returns NULL on OOM (caller handles).
static HexaVal* hexa_array_promote_to_heap(HexaVal* arena_items, int len, int new_cap) {
    HexaVal* heap = (HexaVal*)malloc(sizeof(HexaVal) * (size_t)new_cap);
    if (!heap) return NULL;
    if (arena_items && len > 0) {
        memcpy(heap, arena_items, sizeof(HexaVal) * (size_t)len);
    }
    return heap;
}

HexaVal hexa_array_new() {
    if (_hx_stats_on()) _hx_stats_array_new++;
    HexaVal v = {.tag=TAG_ARRAY};
    HX_SET_ARR_PTR(v, (HexaArr*)calloc(1, sizeof(HexaArr)));
    return v;
}

// ROI #148: shallow-clone a TAG_ARRAY. Gives the caller a fresh HexaArr
// descriptor (own len/cap) and a fresh items buffer of the same logical
// contents. Inner HexaVals (ints/strs/nested arrays) are copied as-is —
// nested arrays remain aliased, matching standard "one level ownership"
// semantics. Non-array inputs are returned unchanged.
//
// Used by hexa_struct_pack_map to break caller→struct-field aliasing on
// struct construction: `Box { items: xs }` stores xs's contents into a
// NEW descriptor so that subsequent hexa_array_push on one struct's
// .items cannot leak into a sibling struct initialized from the same xs.
HexaVal hexa_array_shallow_clone(HexaVal v) {
    if (!HX_IS_ARRAY(v)) return v;
    int n = HX_ARR_LEN(v);
    int src_cap = HX_ARR_CAP(v);
    int real_cap = src_cap < 0 ? -src_cap : src_cap;
    HexaVal out = {.tag=TAG_ARRAY};
    HexaArr* d = (HexaArr*)calloc(1, sizeof(HexaArr));
    if (!d) { fprintf(stderr, "OOM in array_shallow_clone\n"); exit(1); }
    HX_SET_ARR_PTR(out, d);
    if (n == 0 && real_cap == 0) {
        // Empty: leave items NULL, cap 0. Push from here behaves like fresh.
        return out;
    }
    // Always allocate a heap buffer (positive cap) sized to hold the
    // current contents. Avoid the arena-sentinel so downstream push/grow
    // can realloc freely without aliasing the source arena slab.
    int new_cap = real_cap > 0 ? real_cap : (n > 0 ? n : 8);
    if (new_cap < n) new_cap = n;
    HexaVal* items = (HexaVal*)malloc(sizeof(HexaVal) * (size_t)new_cap);
    if (!items) { fprintf(stderr, "OOM in array_shallow_clone items\n"); exit(1); }
    if (n > 0 && HX_ARR_ITEMS(v)) {
        memcpy(items, HX_ARR_ITEMS(v), sizeof(HexaVal) * (size_t)n);
    }
    HX_SET_ARR_ITEMS(out, items);
    HX_SET_ARR_LEN(out, n);
    HX_SET_ARR_CAP(out, new_cap);
    return out;
}

// rt#32-N: snapshot-promote helper. Invoked by the transpiler around every
// `let y = x` and fn-argument expression where x may be a TAG_ARRAY whose items
// buffer is arena-allocated (cap < 0). Deep-copies arena items to malloc so
// the snapshot is insulated from subsequent arena rewinds + callee arena
// re-allocations. Passthrough for non-array / non-arena values.
//
// Rationale: Hexa is pass-by-value at the HexaVal level (I1), but the .items
// pointer inside arr.* is shared. rt#32-M enabled arena-alloc only for the
// slice_fast path because a caller-local snapshot of a persistent global could
// alias the same arena buffer that the callee subsequently re-arena-alloc'd
// (ABA). Snapshot-promote cuts the alias by forcing the caller's copy onto
// the malloc heap at capture time.
HexaVal hexa_val_snapshot_array(HexaVal v) {
    if (!HX_IS_ARRAY(v)) return v;
    if (HX_ARR_CAP(v) >= 0) return v;  // heap-backed — no aliasing with arena rewind
    // cap < 0: arena-backed — promote to heap.
    if (_hx_stats_on()) _hx_stats_array_arena_heapify++;
    int real_cap = -HX_ARR_CAP(v);
    HexaVal* heap = hexa_array_promote_to_heap(HX_ARR_ITEMS(v), HX_ARR_LEN(v), real_cap);
    if (!heap) return v;  // OOM — best-effort, caller may see stale pointer
    HX_SET_ARR_ITEMS(v, heap);
    HX_SET_ARR_CAP(v, real_cap);  // positive → heap
    return v;
}

// rt#32-N: push-arena gate. Default OFF until snapshot-promote is fully wired
// into the transpiler + interpreter source (self/hexa_full.hexa). Set
// HEXA_ARRAY_PUSH_ARENA=1 to enable arena-alloc on first push (cap=0 → cap=-8)
// after snapshot-promote is proven safe.
static int __hexa_array_push_arena_enabled = -1;
static int hexa_array_push_arena_on(void) {
    if (__hexa_array_push_arena_enabled < 0) {
        const char* e = getenv("HEXA_ARRAY_PUSH_ARENA");
        if (!e || !e[0]) __hexa_array_push_arena_enabled = 0;  // default OFF
        else             __hexa_array_push_arena_enabled = (e[0] != '0') ? 1 : 0;
    }
    return __hexa_array_push_arena_enabled;
}

// Optimization #12: reserve capacity up front when size is known.
// rt 32-M: respect arena-backed items (cap<0) — promote to heap on grow.
HexaVal hexa_array_reserve(HexaVal arr, int n) {
    int real_cap = HX_ARR_CAP(arr) < 0 ? -HX_ARR_CAP(arr) : HX_ARR_CAP(arr);
    if (n <= real_cap) return arr;
    if (_hx_stats_on()) _hx_stats_array_reserve++;
    if (HX_ARR_CAP(arr) < 0) {
        if (_hx_stats_on()) _hx_stats_array_arena_promote++;
        HexaVal* heap = hexa_array_promote_to_heap(HX_ARR_ITEMS(arr), HX_ARR_LEN(arr), n);
        if (!heap) { fprintf(stderr, "OOM in array_reserve\n"); exit(1); }
        HX_SET_ARR_ITEMS(arr, heap);
        HX_SET_ARR_CAP(arr, n);
    } else {
        HexaVal* new_items = realloc(HX_ARR_ITEMS(arr), sizeof(HexaVal) * (size_t)n);
        if (!new_items) { fprintf(stderr, "OOM in array_reserve\n"); exit(1); }
        HX_SET_ARR_ITEMS(arr, new_items);
        HX_SET_ARR_CAP(arr, n);
    }
    return arr;
}

// Optimization #12: grow from current cap (2x), not from new_len.
// Only realloc when len exceeds cap; growth factor 2x amortizes cost.
// rt 32-M: arena-alloc the initial slot buffer when a fn-call scope mark is
// live (HEXA_ARRAY_ARENA=1 default). On grow beyond the arena buffer, promote
// to heap with malloc+memcpy — the arena region cannot be realloc'd. Negative
// `cap` encodes from_arena; abs(cap) is the real capacity.
HexaVal hexa_array_push(HexaVal arr, HexaVal item) {
    if (_hx_stats_on()) _hx_stats_array_push++;
    int real_cap = HX_ARR_CAP(arr) < 0 ? -HX_ARR_CAP(arr) : HX_ARR_CAP(arr);
    if (HX_ARR_LEN(arr) >= real_cap) {
        if (_hx_stats_on()) _hx_stats_array_grow++;
        int new_cap = real_cap < 8 ? 8 : real_cap * 2;
        if (HX_ARR_CAP(arr) < 0) {
            // Arena-backed buffer: cannot realloc. Allocate a NEW arena slab
            // if still inside a live mark and there's room; else promote to
            // malloc heap. Keeping it arena-resident preserves the RSS win
            // for arrays that grow incrementally inside the same scope.
            HexaVal* next_items = NULL;
            if (__hexa_val_mark_top > 0 && hexa_array_arena_on()) {
                next_items = hexa_array_arena_alloc_items(new_cap);
            }
            if (next_items) {
                if (HX_ARR_LEN(arr) > 0) {
                    memcpy(next_items, HX_ARR_ITEMS(arr), sizeof(HexaVal) * (size_t)HX_ARR_LEN(arr));
                }
                HX_SET_ARR_ITEMS(arr, next_items);
                HX_SET_ARR_CAP(arr, -new_cap);
            } else {
                // Promote to heap.
                if (_hx_stats_on()) _hx_stats_array_arena_promote++;
                HexaVal* heap = hexa_array_promote_to_heap(HX_ARR_ITEMS(arr), HX_ARR_LEN(arr), new_cap);
                if (!heap) { fprintf(stderr, "OOM in array_push (arena promote)\n"); exit(1); }
                HX_SET_ARR_ITEMS(arr, heap);
                HX_SET_ARR_CAP(arr, new_cap);  // positive → heap
            }
        } else {
            // rt#32-N: push-arena path (cap=0 first-push or existing heap
            // grow). Enabled by HEXA_ARRAY_PUSH_ARENA=1 once the transpiler
            // wraps snapshot-capture sites with hexa_val_snapshot_array.
            // Without snapshot-promote, Hexa pass-by-value (I1) aliasing
            // makes caller snapshots invalid across callee arena rewinds.
            //
            // Only fires on FIRST allocation (cap==0) — subsequent grows on
            // an existing heap buffer continue to realloc as before (no
            // aliasing delta). When mark_top > 0 and arena is on, the fresh
            // buffer is arena-allocated with cap=-new_cap.
            HexaVal* next_items = NULL;
            int use_arena = 0;
            if (real_cap == 0
                && __hexa_val_mark_top > 0
                && hexa_array_arena_on()
                && hexa_array_push_arena_on()) {
                next_items = hexa_array_arena_alloc_items(new_cap);
                if (next_items) {
                    use_arena = 1;
                    if (_hx_stats_on()) _hx_stats_array_arena_alloc++;
                }
            }
            if (use_arena) {
                HX_SET_ARR_ITEMS(arr, next_items);
                HX_SET_ARR_CAP(arr, -new_cap);  // negative → from_arena sentinel
            } else {
                HexaVal* new_items = realloc(HX_ARR_ITEMS(arr), sizeof(HexaVal) * (size_t)new_cap);
                if (!new_items) { fprintf(stderr, "OOM in array_push\n"); exit(1); }
                HX_SET_ARR_ITEMS(arr, new_items);
                HX_SET_ARR_CAP(arr, new_cap);
            }
        }
    }
    HX_ARR_ITEMS(arr)[HX_ARR_LEN(arr)] = item;
    HX_SET_ARR_LEN(arr, HX_ARR_LEN(arr) + 1);
    return arr;
}

// rt 32-B: reusable-buffer scratch push. Semantics identical to hexa_array_push
// except it does NOT bump _hx_stats_array_push / _hx_stats_array_grow (those
// counters are reserved for "true" per-call allocs). Used by the NK_CALL args
// accumulator (call_arg_buf) which is a single long-lived buffer — its growth
// amortises across the whole program and should not inflate the per-call
// push/grow histogram.
HexaVal hexa_array_push_nostat(HexaVal arr, HexaVal item) {
    // rt 32-M: stays on heap (call_arg_buf is long-lived — arena rewind would
    // invalidate it). If an arena buffer somehow lands here, promote to heap.
    int real_cap = HX_ARR_CAP(arr) < 0 ? -HX_ARR_CAP(arr) : HX_ARR_CAP(arr);
    if (HX_ARR_LEN(arr) >= real_cap) {
        int new_cap = real_cap < 8 ? 8 : real_cap * 2;
        if (HX_ARR_CAP(arr) < 0) {
            HexaVal* heap = hexa_array_promote_to_heap(HX_ARR_ITEMS(arr), HX_ARR_LEN(arr), new_cap);
            if (!heap) { fprintf(stderr, "OOM in array_push_nostat\n"); exit(1); }
            HX_SET_ARR_ITEMS(arr, heap);
            HX_SET_ARR_CAP(arr, new_cap);
        } else {
            HexaVal* new_items = realloc(HX_ARR_ITEMS(arr), sizeof(HexaVal) * (size_t)new_cap);
            if (!new_items) { fprintf(stderr, "OOM in array_push_nostat\n"); exit(1); }
            HX_SET_ARR_ITEMS(arr, new_items);
            HX_SET_ARR_CAP(arr, new_cap);
        }
    }
    HX_ARR_ITEMS(arr)[HX_ARR_LEN(arr)] = item;
    HX_SET_ARR_LEN(arr, HX_ARR_LEN(arr) + 1);
    return arr;
}

// rt 32-B: bulk slice primitive — single malloc + memcpy, counts as exactly
// ONE array_new (not N array_push). Used to hand a per-call args view out of
// the shared call_arg_buf so the callee receives an independent backing store
// that is not disturbed by subsequent NK_CALL recursion mutating the buffer.
HexaVal hexa_array_slice_fast(HexaVal arr, HexaVal start, HexaVal end) {
    if (_hx_stats_on()) _hx_stats_array_new++;
    HexaVal out = {.tag=TAG_ARRAY};
    HX_SET_ARR_PTR(out, (HexaArr*)calloc(1, sizeof(HexaArr)));
    if (!HX_IS_ARRAY(arr)) return out;
    int n = HX_ARR_LEN(arr);
    int a = (int)HX_INT(start), b = (int)HX_INT(end);
    if (a < 0) a = 0;
    if (b > n) b = n;
    if (a > b) a = b;
    int m = b - a;
    if (m <= 0) return out;
    // rt 32-M: arena-allocate slice items when inside a live fn-scope mark.
    // Slice_fast is used primarily for the NK_CALL scratch-args pattern (rt 32-B)
    // where the slice is immediately handed to call_user_fn as args, consumed
    // (each element copied into env_define), and discarded on fn return — a
    // lifetime that fits exactly within the callee's mark frontier.
    //
    // Safety: call_stack.slice(...) at call_user_fn exit happens BEFORE the
    // __HEXA_ARENA_POP__, so the caller sees a slice whose items pointer is
    // NOT rewound. But call_stack.slice is built from truncation on line 8341
    // AFTER the pop — so it runs at outer mark. We guard by only arena-
    // allocating when mark_top > 0 AND the SOURCE array is heap (cap >= 0).
    // For call_stack.slice(0, len-1) after pop, the source is heap; the result
    // lives at the outer's arena frontier which the outer's pop eventually
    // reclaims — call_stack is reassigned every call anyway, so the old slice
    // is unreachable by the next cycle. Heapify-on-return handles fn-return
    // escapes.
    HexaVal* items = NULL;
    int use_arena = 0;
    if (hexa_array_arena_on() && __hexa_val_mark_top > 0) {
        items = hexa_array_arena_alloc_items(m);
        if (items) { use_arena = 1; if (_hx_stats_on()) _hx_stats_array_arena_alloc++; }
    }
    if (!items) {
        items = (HexaVal*)malloc(sizeof(HexaVal) * (size_t)m);
        if (!items) { fprintf(stderr, "OOM in array_slice_fast\n"); exit(1); }
    }
    memcpy(items, HX_ARR_ITEMS(arr) + a, sizeof(HexaVal) * (size_t)m);
    HX_SET_ARR_ITEMS(out, items);
    HX_SET_ARR_LEN(out, m);
    HX_SET_ARR_CAP(out, use_arena ? -m : m);
    return out;
}

// Forward decl — hexa_throw 본체는 line ~2903. OOB throw routing 을 위해
// 여기서 일찍 선언.
void hexa_throw(HexaVal err);

HexaVal hexa_array_get(HexaVal arr, int64_t idx) {
    // B13: tag guard — without this, a TAG_STR (from hexa_add fallthrough)
    // reaches here and we read .arr.len from an unrelated union slot
    // (uninitialized stack residue). 이전 exit(1) 대신 hexa_throw 로 라우팅
    // 해서 try/catch 로 recovery 가능.
    if (!HX_IS_ARRAY(arr)) {
        char _buf[128];
        snprintf(_buf, sizeof(_buf), "array[%lld]: container is not an array (tag=%d)", (long long)idx, (int)HX_TAG(arr));
        hexa_throw(hexa_str(_buf));
        return hexa_void();
    }
    if (idx < 0) idx += HX_ARR_LEN(arr);
    if (idx < 0 || idx >= HX_ARR_LEN(arr)) {
        char _buf[128];
        snprintf(_buf, sizeof(_buf), "index %lld out of bounds (len %d)", (long long)idx, HX_ARR_LEN(arr));
        hexa_throw(hexa_str(_buf));
        return hexa_void();
    }
    return HX_ARR_ITEMS(arr)[idx];
}

HexaVal hexa_array_set(HexaVal arr, int64_t idx, HexaVal val) {
    if (!HX_IS_ARRAY(arr)) {
        char _buf[128];
        snprintf(_buf, sizeof(_buf), "array_set[%lld]: container is not an array (tag=%d)", (long long)idx, (int)HX_TAG(arr));
        hexa_throw(hexa_str(_buf));
        return arr;
    }
    if (idx < 0) idx += HX_ARR_LEN(arr);
    if (idx < 0 || idx >= HX_ARR_LEN(arr)) {
        char _buf[128];
        snprintf(_buf, sizeof(_buf), "index %lld out of bounds (len %d)", (long long)idx, HX_ARR_LEN(arr));
        hexa_throw(hexa_str(_buf));
        return arr;
    }
    // In-place mutate (no copy — caller reassigns)
    HX_ARR_ITEMS(arr)[idx] = val;
    return arr;
}

// bt 55 — in-place truncate. Used by env_pop_scope to avoid the
// `new_vars.push` rebuild loop which realloc-leaks the old backing buffer
// every scope pop. Retains capacity for amortized re-growth.
// No free of arr.items here (HexaVal entries may still be shared); we only
// shrink the logical length. Safe because the slots beyond `new_len` become
// unreachable through this handle.
HexaVal hexa_array_truncate(HexaVal arr, HexaVal new_len_v) {
    if (!HX_IS_ARRAY(arr)) return arr;
    int64_t new_len = HX_IS_INT(new_len_v) ? HX_INT(new_len_v) : (int64_t)HX_INT(new_len_v);
    if (new_len < 0) new_len = 0;
    if (new_len > HX_ARR_LEN(arr)) new_len = HX_ARR_LEN(arr);
    HX_SET_ARR_LEN(arr, (int)new_len);
    return arr;
}

int hexa_len(HexaVal v) {
    if (HX_IS_STR(v)) return strlen(HX_STR(v));
    if (HX_IS_ARRAY(v)) return HX_ARR_LEN(v);
    if (HX_IS_MAP(v)) return HX_MAP_LEN(v);
    return 0;
}

// ── Map operations (Optimization #10: hash table) ───────

// rt 32-L: forward decls — Val arena lives below the str_concat arena
// (shared block chain via hexa_arena_alloc) but maintains its own scope-mark
// stack so str-concat marks aren't perturbed by Val scope push/pop.
void* hexa_arena_alloc(size_t n);  // defined below; forward decl for hmap_alloc_ex.
static int hexa_val_arena_on(void);
static void* hexa_val_arena_calloc(size_t n);
extern int __hexa_val_mark_top;  // defined later; gate guard for module-init Vals.
extern int __hexa_val_force_heap; // T33-fix-2

// Allocate a new HexaMapTable with given hash-table capacity.
// rt 32-L: when from_arena=1, use bump-arena instead of malloc/calloc. Caller
// guarantees the table will be heapified (via hexa_val_heapify) before the
// arena rewind point catches up. When from_arena=0, behavior is unchanged.
static HexaMapTable* hmap_alloc_ex(int ht_cap, int from_arena) {
    HexaMapTable* t;
    int order_init = ht_cap < 8 ? 8 : ht_cap;
    if (from_arena) {
        // All four allocations satisfied from the same bump frontier — single
        // O(1) reset frees them in bulk. Zeroing via memset is required to
        // satisfy the pre-existing calloc guarantees that hmap_find / load-
        // factor checks rely on (slots[].key == NULL means empty).
        t = (HexaMapTable*)hexa_val_arena_calloc(sizeof(HexaMapTable));
        t->slots = (HexaMapSlot*)hexa_val_arena_calloc(ht_cap * sizeof(HexaMapSlot));
        t->vals  = (HexaVal*)hexa_val_arena_calloc(ht_cap * sizeof(HexaVal));
        t->order_keys = (char**)hexa_val_arena_calloc(sizeof(char*) * order_init);
        t->order_vals = (HexaVal*)hexa_val_arena_calloc(sizeof(HexaVal) * order_init);
        t->from_arena = 1;
    } else {
        t = (HexaMapTable*)calloc(1, sizeof(HexaMapTable));
        t->slots = (HexaMapSlot*)calloc(ht_cap, sizeof(HexaMapSlot));
        t->vals  = (HexaVal*)calloc(ht_cap, sizeof(HexaVal));
        t->order_keys = (char**)malloc(sizeof(char*) * order_init);
        t->order_vals = (HexaVal*)malloc(sizeof(HexaVal) * order_init);
        t->from_arena = 0;
    }
    t->ht_cap = ht_cap;
    t->order_cap = order_init;
    t->len = 0;
    return t;
}

// Backward-compat wrapper — preserves the heap-only signature for callers that
// must guarantee long-lived storage (env caches, fn body maps, etc.).
static HexaMapTable* hmap_alloc(int ht_cap) {
    return hmap_alloc_ex(ht_cap, 0);
}

// Rehash the table to double its capacity.
static void hmap_grow(HexaMapTable* t) {
    int old_cap = t->ht_cap;
    HexaMapSlot* old_slots = t->slots;
    HexaVal* old_vals = t->vals;
    int new_cap = old_cap * 2;
    HexaMapSlot* new_slots = (HexaMapSlot*)calloc(new_cap, sizeof(HexaMapSlot));
    HexaVal* new_vals = (HexaVal*)calloc(new_cap, sizeof(HexaVal));
    uint32_t mask = (uint32_t)(new_cap - 1);
    for (int i = 0; i < old_cap; i++) {
        if (old_slots[i].key) {
            uint32_t idx = old_slots[i].hash & mask;
            while (new_slots[idx].key) idx = (idx + 1) & mask;
            new_slots[idx] = old_slots[i];
            new_vals[idx]  = old_vals[i];
        }
    }
    // T33: do NOT free arena-allocated old buffers — free() on arena mem is UB.
    // The arena will reclaim them on scope pop.
    if (!t->from_arena) {
        free(old_slots);
        free(old_vals);
    }
    t->slots = new_slots;
    t->vals  = new_vals;
    t->ht_cap = new_cap;
    // Mark table as heap-backed for future growth/free decisions — the slots/
    // vals/order arrays may still be arena, but new_slots/new_vals here are
    // heap, so subsequent free(t->slots) after arena pop would be safe. Keep
    // from_arena unchanged since order_keys/order_vals still carry the
    // original allocation site.
}

// Find slot index for key, or -1 if not found.
static int hmap_find(HexaMapTable* t, const char* key, uint32_t h) {
    uint32_t mask = (uint32_t)(t->ht_cap - 1);
    uint32_t idx = h & mask;
    while (t->slots[idx].key) {
        if (t->slots[idx].hash == h && strcmp(t->slots[idx].key, key) == 0)
            return (int)idx;
        idx = (idx + 1) & mask;
    }
    return -1;
}

HexaVal hexa_map_new() {
    if (_hx_stats_on()) _hx_stats_map_new++;
    HexaVal v = {.tag=TAG_MAP};
    HX_SET_MAP_PTR(v, (HexaMap*)calloc(1, sizeof(HexaMap)));
    return v;
}

// rt 32-A: bulk struct constructor — collapses 1 map_new + N map_set into a
// single pre-sized table allocation + tight insertion loop. Used by
// gen2_struct_decl for `Val { ... }` and other struct literals. Avoids N
// successive hexa_map_set calls (each with its own stats bump + load-factor
// check). Key array is NOT freed; strings must be string literals (stable).
HexaVal hexa_struct_pack_map(const char* type_name, int n,
                             const char* const* keys, const HexaVal* vals) {
    if (_hx_stats_on()) _hx_stats_map_new++;
    HexaVal v = {.tag=TAG_MAP};
    HX_SET_MAP_PTR(v, (HexaMap*)calloc(1, sizeof(HexaMap)));
    // Pre-size: (n+1) entries including __type__, target load < HMAP_LOAD_MAX/100.
    // Solve for smallest power-of-2 cap where (n+1)*100/cap < HMAP_LOAD_MAX.
    int need = (n + 1) * 100 / HMAP_LOAD_MAX + 1;
    int cap = HMAP_INIT_CAP;
    while (cap < need) cap <<= 1;
    // rt 32-L: opt-in arena allocation for struct literals. Anonymous map
    // literals (type_name == "" or NULL) are excluded — they often back
    // long-lived data (config, AST nodes, registries) where the heapify
    // discipline is not currently wired. Type_named struct literals are the
    // hot fib path (every Val{...} in the interpreter) and the primary RSS
    // contributor at K=30. The arena gate is HEXA_VAL_ARENA (separate from
    // HEXA_STR_ARENA so the two can be tuned independently).
    int from_arena = (type_name && type_name[0]) ?
                     (hexa_val_arena_on() && __hexa_val_mark_top > 0) : 0;
    HexaMapTable* t = hmap_alloc_ex(cap, from_arena);
    HX_SET_MAP_TBL(v, t);
    uint32_t mask = (uint32_t)(cap - 1);
    // Insert __type__ first (skip for anonymous map literals where
    // type_name is "" — they don't carry a __type__ field).
    if (type_name && type_name[0]) {
        uint32_t h = hexa_fnv1a_str("__type__");
        uint32_t idx = h & mask;
        while (t->slots[idx].key) idx = (idx + 1) & mask;
        // Keys/values strdup'd into heap regardless of from_arena: heapify()
        // can shallow-share these strings (keys are usually literal-like and
        // the arena would otherwise force per-string heap copies on every
        // heapify call, defeating the win). The TAG_STR value `tv` payload is
        // also strdup'd; for arena path we instead arena-alloc to keep the
        // bulk-free property.
        if (from_arena) {
            char* kdup = (char*)hexa_arena_alloc(strlen("__type__") + 1);
            memcpy(kdup, "__type__", 9);
            t->slots[idx].key = kdup;
            HexaVal tv = {.tag=TAG_STR};
            size_t tnl = strlen(type_name);
            char* tdup = (char*)hexa_arena_alloc(tnl + 1);
            memcpy(tdup, type_name, tnl + 1);
            HX_SET_STR(tv, tdup);
            t->slots[idx].hash = h;
            t->slots[idx].order_idx = t->len;  // ROI-24
            t->vals[idx] = tv;
            t->order_keys[t->len] = t->slots[idx].key;
            t->order_vals[t->len] = tv;
        } else {
            t->slots[idx].key = strdup("__type__");
            t->slots[idx].hash = h;
            t->slots[idx].order_idx = t->len;  // ROI-24
            HexaVal tv = {.tag=TAG_STR};
            HX_SET_STR(tv, strdup(type_name));
            t->vals[idx] = tv;
            t->order_keys[t->len] = t->slots[idx].key;
            t->order_vals[t->len] = tv;
        }
        t->len++;
    }
    for (int i = 0; i < n; i++) {
        const char* k = keys[i];
        uint32_t h = hexa_fnv1a_str(k);
        uint32_t idx = h & mask;
        while (t->slots[idx].key) idx = (idx + 1) & mask;
        if (from_arena) {
            size_t kl = strlen(k);
            char* kdup = (char*)hexa_arena_alloc(kl + 1);
            memcpy(kdup, k, kl + 1);
            t->slots[idx].key = kdup;
        } else {
            t->slots[idx].key = strdup(k);
        }
        t->slots[idx].hash = h;
        t->slots[idx].order_idx = t->len;  // ROI-24
        // ROI #148: break caller→struct-field array aliasing. A struct
        // literal's array fields must own a private backing buffer so
        // that `.push` mutations on one instance cannot leak into a
        // sibling struct built from the same source array.
        // Type-named struct literals only — anonymous map literals
        // (type_name=="") preserve raw-store semantics (map of scalars,
        // no ownership contract).
        HexaVal stored = vals[i];
        if (type_name && type_name[0] && HX_IS_ARRAY(stored)) {
            stored = hexa_array_shallow_clone(stored);
        }
        t->vals[idx] = stored;
        t->order_keys[t->len] = t->slots[idx].key;
        t->order_vals[t->len] = stored;
        t->len++;
    }
    HX_SET_MAP_LEN(v, t->len);
    return v;
}

HexaVal hexa_map_set(HexaVal m, const char* key, HexaVal val) {
    // rt 32-G: route Val field mutation to flat struct.
    if (HX_IS_VALSTRUCT(m)) {
        return hexa_valstruct_set_by_key(m, key, val);
    }
    if (_hx_stats_on()) _hx_stats_map_set++;
    // Lazy-alloc table on first insert
    if (!HX_MAP_TBL(m)) {
        HX_SET_MAP_TBL(m, hmap_alloc(HMAP_INIT_CAP));
    }
    HexaMapTable* t = HX_MAP_TBL(m);
    uint32_t h = hexa_fnv1a_str(key);

    // Check if key exists (O(1) average)
    int si = hmap_find(t, key, h);
    if (si >= 0) {
        t->vals[si] = val;
        // ROI-24: O(1) order-array update via cached order_idx
        t->order_vals[t->slots[si].order_idx] = val;
        return m;
    }

    // Grow hash table if needed
    if (t->len * 100 / t->ht_cap >= HMAP_LOAD_MAX) {
        hmap_grow(t);
    }

    // Insert into hash table
    uint32_t mask = (uint32_t)(t->ht_cap - 1);
    uint32_t idx = h & mask;
    while (t->slots[idx].key) idx = (idx + 1) & mask;
    t->slots[idx].key  = strdup(key);
    t->slots[idx].hash = h;
    t->slots[idx].order_idx = t->len;  // ROI-24: record order position
    t->vals[idx] = val;

    // Append to insertion-order arrays
    if (t->len >= t->order_cap) {
        int new_cap = t->order_cap * 2;
        if (t->from_arena) {
            // T33: arena-backed tables cannot realloc — promote order arrays
            // to heap before grow. After promotion from_arena stays 1 for
            // slots/vals (still arena) but the order_* arrays are now heap.
            // The final heapify() will malloc-copy everything anyway; this
            // only handles the mid-scope growth case.
            char** new_keys = (char**)malloc(sizeof(char*) * new_cap);
            HexaVal* new_vals = (HexaVal*)malloc(sizeof(HexaVal) * new_cap);
            if (!new_keys || !new_vals) { fprintf(stderr, "OOM in map_set grow\n"); exit(1); }
            memcpy(new_keys, t->order_keys, sizeof(char*) * t->len);
            memcpy(new_vals, t->order_vals, sizeof(HexaVal) * t->len);
            t->order_keys = new_keys;
            t->order_vals = new_vals;
        } else {
            t->order_keys = (char**)realloc(t->order_keys, sizeof(char*) * new_cap);
            t->order_vals = (HexaVal*)realloc(t->order_vals, sizeof(HexaVal) * new_cap);
        }
        t->order_cap = new_cap;
    }
    t->order_keys[t->len] = t->slots[idx].key;  // shared pointer, not a copy
    t->order_vals[t->len] = val;
    t->len++;
    HX_SET_MAP_LEN(m, t->len);
    return m;
}

HexaVal hexa_map_get(HexaVal m, const char* key) {
    // rt 32-G: route Val field access to flat struct accessor.
    if (HX_IS_VALSTRUCT(m)) {
        return hexa_valstruct_get_by_key(m, key);
    }
    if (!HX_MAP_TBL(m)) {
        fprintf(stderr, "map key '%s' not found\n", key);
        return hexa_void();
    }
    HexaMapTable* t = HX_MAP_TBL(m);
    uint32_t h = hexa_fnv1a_str(key);
    int si = hmap_find(t, key, h);
    if (si >= 0) return t->vals[si];
    fprintf(stderr, "map key '%s' not found\n", key);
    return hexa_void();
}

// ── rt#37: Inline cache for field access ─────────────────
// Each call site of `obj.field` gets a static HexaIC slot. On hit, we skip
// the strcmp-chain and return m.map.vals[ic->idx] directly. On miss we fall
// back to hexa_map_get and repopulate. Shape check uses (keys_ptr, len) —
// a struct constructor inserts keys in a fixed order, so once its (keys, len)
// is stable, idx is valid for every subsequent access.
//
// Invalidation: if keys pointer or len differ, we treat it as a miss. That
// covers (a) a different struct instance reused through the same call site
// and (b) a struct mutated via hexa_map_set (which may realloc the keys
// array). A monomorphic call site converges to ~100% hit rate after 1 call.
//
// Gate: HEXA_IC_STATS=1 dumps per-process hit/miss totals at exit.

static uint64_t g_hexa_ic_hits   = 0;
static uint64_t g_hexa_ic_misses = 0;
static int      g_hexa_ic_stats_enabled   = -1; // -1=unchecked, 0=off, 1=on
static int      g_hexa_ic_stats_installed = 0;

static void hexa_ic_dump_stats(void) {
    uint64_t total = g_hexa_ic_hits + g_hexa_ic_misses;
    double rate = total ? (100.0 * (double)g_hexa_ic_hits / (double)total) : 0.0;
    fprintf(stderr,
        "[rt#37 IC] hits=%llu misses=%llu total=%llu hit_rate=%.2f%%\n",
        (unsigned long long)g_hexa_ic_hits,
        (unsigned long long)g_hexa_ic_misses,
        (unsigned long long)total,
        rate);
}

static inline int hexa_ic_stats_on(void) {
    if (g_hexa_ic_stats_enabled < 0) {
        const char* on = getenv("HEXA_IC_STATS");
        g_hexa_ic_stats_enabled = (on && on[0] != '0' && on[0] != '\0') ? 1 : 0;
        if (g_hexa_ic_stats_enabled && !g_hexa_ic_stats_installed) {
            g_hexa_ic_stats_installed = 1;
            atexit(hexa_ic_dump_stats);
        }
    }
    return g_hexa_ic_stats_enabled;
}

// Slow path: full scan + update cache. Kept out-of-line to shrink the inline
// hot path macro footprint. Stats bump lives here so hits stay counter-free
// when HEXA_IC_STATS is unset.
// rt#32-N build-fix: rt#37 referenced m.map.{keys,vals} which were removed by
// rt#32-G when map storage moved to HexaMapTable. Redirect through .tbl.
static HexaVal hexa_map_get_ic_slow(HexaVal m, const char* key, HexaIC* ic) {
    if (hexa_ic_stats_on()) { ic->misses++; g_hexa_ic_misses++; }
    if (HX_IS_MAP(m) && HX_MAP_TBL(m)) {
        HexaMapTable* t = HX_MAP_TBL(m);
        // ROI-43: O(1) avg via hmap_find instead of O(n) linear scan.
        uint32_t h = hexa_fnv1a_str(key);
        int si = hmap_find(t, key, h);
        if (si >= 0) {
            int oi = t->slots[si].order_idx;  // ROI-24 cached order index
            ic->keys_ptr = (void*)t->order_keys;
            ic->len      = t->len;
            ic->idx      = oi;
            return t->order_vals[oi];
        }
    }
    return hexa_map_get(m, key);
}

// Hot path as a MACRO so the fast path avoids the 32-byte HexaVal call ABI
// altogether. We evaluate `m` exactly once via __ic_m to be safe with side
// effects, then:
//   - if (m.map.keys == ic->keys_ptr && m.map.len == ic->len) -> return cached
//   - else                                                   -> slow fallback
// The stat bump on the hot path is compiled out when HEXA_IC_STATS is unset
// because g_hexa_ic_stats_enabled flips to 0 on first slow path miss.
// rt#32-N build-fix: m.map.{keys,vals} -> m.map.tbl->{order_keys,order_vals}
#define hexa_map_get_ic(M, KEY, IC) \
    ({ HexaVal __ic_m = (M); HexaIC* __ic = (IC); \
       (HX_MAP_TBL(__ic_m) \
        && (void*)HX_MAP_TBL(__ic_m)->order_keys == __ic->keys_ptr \
        && HX_MAP_TBL(__ic_m)->len == __ic->len \
        && __ic->idx < __ic->len) \
           ? (g_hexa_ic_stats_enabled > 0 \
              ? (__ic->hits++, g_hexa_ic_hits++, HX_MAP_TBL(__ic_m)->order_vals[__ic->idx]) \
              : HX_MAP_TBL(__ic_m)->order_vals[__ic->idx]) \
           : hexa_map_get_ic_slow(__ic_m, (KEY), __ic); })


HexaVal hexa_map_keys(HexaVal m) {
    HexaVal arr = hexa_array_new();
    if (!HX_MAP_TBL(m)) return arr;
    HexaMapTable* t = HX_MAP_TBL(m);
    arr = hexa_array_reserve(arr, t->len);
    for (int i = 0; i < t->len; i++) {
        arr = hexa_array_push(arr, hexa_str(t->order_keys[i]));
    }
    return arr;
}

HexaVal hexa_map_values(HexaVal m) {
    HexaVal arr = hexa_array_new();
    if (!HX_MAP_TBL(m)) return arr;
    HexaMapTable* t = HX_MAP_TBL(m);
    arr = hexa_array_reserve(arr, t->len);
    for (int i = 0; i < t->len; i++) {
        arr = hexa_array_push(arr, t->order_vals[i]);
    }
    return arr;
}

int hexa_map_contains_key(HexaVal m, const char* key) {
    if (!HX_MAP_TBL(m)) return 0;
    uint32_t h = hexa_fnv1a_str(key);
    return hmap_find(HX_MAP_TBL(m), key, h) >= 0;
}

// entries: flat array of [key, value] pairs. Keys come out as strings
// (HexaMap stores keys as char*). Matches interpreter semantics at
// self/hexa_full.hexa:15558-15568.
HexaVal hexa_map_entries(HexaVal m) {
    HexaVal out = hexa_array_new();
    if (!HX_MAP_TBL(m)) return out;
    HexaMapTable* t = HX_MAP_TBL(m);
    for (int i = 0; i < t->len; i++) {
        HexaVal pair = hexa_array_new();
        pair = hexa_array_push(pair, hexa_str(t->order_keys[i]));
        pair = hexa_array_push(pair, t->order_vals[i]);
        out = hexa_array_push(out, pair);
    }
    return out;
}

// to_array: same shape as entries() — keeps names consistent with
// interpreter dispatch (self/hexa_full.hexa:15611-15620).
HexaVal hexa_map_to_array(HexaVal m) {
    return hexa_map_entries(m);
}

// merge: overlay other's entries onto self. Other wins on key collision.
// Matches interpreter at self/hexa_full.hexa:15570-15583. Non-map `other`
// returns self unchanged.
HexaVal hexa_map_merge(HexaVal a, HexaVal b) {
    if (!HX_MAP_TBL(a)) return a;
    if (!HX_MAP_TBL(b)) return a;
    HexaVal out = a;
    HexaMapTable* tb = HX_MAP_TBL(b);
    for (int i = 0; i < tb->len; i++) {
        out = hexa_map_set(out, tb->order_keys[i], tb->order_vals[i]);
    }
    return out;
}

// map_values(fn): apply fn to every value, return new map with same keys.
// Matches interpreter at self/hexa_full.hexa:15584-15594.
HexaVal hexa_map_map_values(HexaVal m, HexaVal fn) {
    HexaVal out = hexa_map_new();
    if (!HX_MAP_TBL(m)) return out;
    HexaMapTable* t = HX_MAP_TBL(m);
    for (int i = 0; i < t->len; i++) {
        HexaVal new_val = hexa_call1(fn, t->order_vals[i]);
        out = hexa_map_set(out, t->order_keys[i], new_val);
    }
    return out;
}

// filter_keys(fn): keep only entries where fn(key) is truthy.
// Matches interpreter at self/hexa_full.hexa:15595-15610. Key is passed
// to predicate as a string value (HexaMap stores keys as char*).
HexaVal hexa_map_filter_keys(HexaVal m, HexaVal fn) {
    HexaVal out = hexa_map_new();
    if (!HX_MAP_TBL(m)) return out;
    HexaMapTable* t = HX_MAP_TBL(m);
    for (int i = 0; i < t->len; i++) {
        HexaVal key_val = hexa_str(t->order_keys[i]);
        if (hexa_truthy(hexa_call1(fn, key_val))) {
            out = hexa_map_set(out, t->order_keys[i], t->order_vals[i]);
        }
    }
    return out;
}

// invert: swap k<->v. Values get stringified via hexa_to_string to serve
// as new keys (HexaMap keys are char*); original keys become string vals.
// Matches interpreter at self/hexa_full.hexa:15641-15645.
HexaVal hexa_map_invert(HexaVal m) {
    HexaVal out = hexa_map_new();
    if (!HX_MAP_TBL(m)) return out;
    HexaMapTable* t = HX_MAP_TBL(m);
    for (int i = 0; i < t->len; i++) {
        const char* new_key = hexa_str_as_ptr(hexa_to_string(t->order_vals[i]));
        if (!new_key) continue;
        out = hexa_map_set(out, new_key, hexa_str(t->order_keys[i]));
    }
    return out;
}

// from_array: build a map from an array of [k, v] pair arrays. Receiver
// is ignored (called as-method on an empty map per interpreter dispatch).
// Malformed pairs (non-array or <2 elements) are skipped. Keys are
// stringified. Matches interpreter at self/hexa_full.hexa:15622-15640.
HexaVal hexa_map_from_array(HexaVal self_map, HexaVal arr) {
    (void)self_map;
    HexaVal out = hexa_map_new();
    if (!HX_IS_ARRAY(arr)) return out;
    int n = HX_ARR_LEN(arr);
    for (int i = 0; i < n; i++) {
        HexaVal pair = HX_ARR_ITEMS(arr)[i];
        if (!HX_IS_ARRAY(pair) || HX_ARR_LEN(pair) < 2) continue;
        HexaVal k = HX_ARR_ITEMS(pair)[0];
        HexaVal v = HX_ARR_ITEMS(pair)[1];
        const char* k_str = hexa_str_as_ptr(hexa_to_string(k));
        if (!k_str) continue;
        out = hexa_map_set(out, k_str, v);
    }
    return out;
}

// count(pred): pred is 2-arg fn(key, value) returning truthy for hits.
// No-pred variant (arg is void sentinel) returns total entry count.
// Keys are exposed to the predicate as TAG_STR per interpreter semantics.
// Matches interpreter at self/hexa_full.hexa:15689-15704.
HexaVal hexa_map_count(HexaVal m, HexaVal pred) {
    if (!HX_MAP_TBL(m)) return hexa_int(0);
    HexaMapTable* t = HX_MAP_TBL(m);
    if (HX_IS_VOID(pred)) return hexa_int(t->len);
    int cnt = 0;
    for (int i = 0; i < t->len; i++) {
        HexaVal key_val = hexa_str(t->order_keys[i]);
        HexaVal r = hexa_call2(pred, key_val, t->order_vals[i]);
        if (hexa_truthy(r)) cnt++;
    }
    return hexa_int(cnt);
}

// any(pred): pred is 2-arg fn(key, value). Returns true on first truthy
// result, else false. Empty map → false.
// Matches interpreter at self/hexa_full.hexa:15705-15718.
HexaVal hexa_map_any(HexaVal m, HexaVal pred) {
    if (!HX_MAP_TBL(m)) return hexa_bool(0);
    HexaMapTable* t = HX_MAP_TBL(m);
    if (HX_IS_VOID(pred)) return hexa_bool(0);
    for (int i = 0; i < t->len; i++) {
        HexaVal key_val = hexa_str(t->order_keys[i]);
        HexaVal r = hexa_call2(pred, key_val, t->order_vals[i]);
        if (hexa_truthy(r)) return hexa_bool(1);
    }
    return hexa_bool(0);
}

// all(pred): pred is 2-arg fn(key, value). Returns false on first falsy
// result, else true. Empty map → true (vacuous).
// Matches interpreter at self/hexa_full.hexa:15719-15732.
HexaVal hexa_map_all(HexaVal m, HexaVal pred) {
    if (!HX_MAP_TBL(m)) return hexa_bool(1);
    HexaMapTable* t = HX_MAP_TBL(m);
    if (HX_IS_VOID(pred)) return hexa_bool(1);
    for (int i = 0; i < t->len; i++) {
        HexaVal key_val = hexa_str(t->order_keys[i]);
        HexaVal r = hexa_call2(pred, key_val, t->order_vals[i]);
        if (!hexa_truthy(r)) return hexa_bool(0);
    }
    return hexa_bool(1);
}

HexaVal hexa_map_remove(HexaVal m, const char* key) {
    if (!HX_MAP_TBL(m)) return m;
    HexaMapTable* t = HX_MAP_TBL(m);
    uint32_t h = hexa_fnv1a_str(key);
    int si = hmap_find(t, key, h);
    if (si < 0) return m;

    // ROI-24: O(1) locate in order array via cached order_idx, then shift
    int oi = t->slots[si].order_idx;
    for (int j = oi; j < t->len - 1; j++) {
        t->order_keys[j] = t->order_keys[j+1];
        t->order_vals[j] = t->order_vals[j+1];
    }
    // Fix order_idx for all hash-table slots whose order position shifted
    for (int j = 0; j < t->ht_cap; j++) {
        if (t->slots[j].key && t->slots[j].order_idx > oi) {
            t->slots[j].order_idx--;
        }
    }

    // Remove from hash table: mark slot empty and re-insert displaced entries
    free(t->slots[si].key);
    t->slots[si].key = NULL;
    t->slots[si].hash = 0;
    // Robin Hood deletion: re-probe subsequent slots
    uint32_t mask = (uint32_t)(t->ht_cap - 1);
    uint32_t ci = ((uint32_t)si + 1) & mask;
    while (t->slots[ci].key) {
        // Remove and re-insert this entry
        HexaMapSlot saved_slot = t->slots[ci];
        HexaVal saved_val = t->vals[ci];
        t->slots[ci].key = NULL;
        t->slots[ci].hash = 0;
        uint32_t ri = saved_slot.hash & mask;
        while (t->slots[ri].key) ri = (ri + 1) & mask;
        t->slots[ri] = saved_slot;  // ROI-24: order_idx carried in struct copy
        t->vals[ri] = saved_val;
        ci = (ci + 1) & mask;
    }

    t->len--;
    HX_SET_MAP_LEN(m, t->len);
    return m;
}

// Polymorphic index get: array[int] or map[string]
HexaVal hexa_index_get(HexaVal container, HexaVal key) {
    if (HX_IS_MAP(container) && HX_IS_STR(key)) {
        return hexa_map_get(container, HX_STR(key));
    }
    // 2026-04-17: unwrap interpreter Val wrapper (TAG_VALSTRUCT) so HX_INT
    // doesn't read .vs as a pointer-as-int garbage index. Same root as the
    // hexa_cmp_* fix — without this, val_int's `__VAL_INT_CACHE[n]` lookup
    // OOB-aborted when n was a wrapped Val.
    int64_t idx;
    if (HX_IS_VALSTRUCT(key) && HX_VS(key)) {
        idx = (HX_VSF(key, tag_i) == TAG_INT)
            ? HX_VSF(key, int_val)
            : (int64_t)__hx_to_double(key);
    } else {
        idx = HX_INT(key);
    }
    // hxa-20260423-015: str[i] support. The AOT codegen lowers every
    // IndexGet to hexa_index_get(), so without this branch a `path[i]`
    // on a string (e.g. self/module_loader.hexa:ml_dirname) reached
    // hexa_array_get() and aborted with "array[N]: container is not
    // an array (tag=3)". The interp interpreter has its own string-
    // index path; this mirrors it for the AOT path.
    if (HX_IS_STR(container)) {
        return hexa_str_char_at(container, hexa_int(idx));
    }
    return hexa_array_get(container, idx);
}

// For-in dispatch: array -> element, map -> key as string.
// Uses insertion-order arrays so maps iterate in insertion order (Python-style).
HexaVal hexa_iter_get(HexaVal v, int64_t idx) {
    if (HX_IS_MAP(v)) {
        if (!HX_MAP_TBL(v) || idx < 0 || idx >= HX_MAP_LEN(v)) {
            fprintf(stderr, "map iter index %lld out of bounds (len %d)\n", (long long)idx, HX_MAP_LEN(v));
            exit(1);
        }
        return hexa_str(HX_MAP_TBL(v)->order_keys[idx]);
    }
    return hexa_array_get(v, idx);
}

// Polymorphic index set: array[int]=val or map[string]=val
HexaVal hexa_index_set(HexaVal container, HexaVal key, HexaVal val) {
    if (HX_IS_MAP(container) && HX_IS_STR(key)) {
        return hexa_map_set(container, HX_STR(key), val);
    }
    // 2026-04-17: unwrap VALSTRUCT same as hexa_index_get — without this,
    // HX_INT reads raw .vs pointer as index → "index 105553... out of bounds"
    int64_t idx;
    if (HX_IS_VALSTRUCT(key) && HX_VS(key)) {
        idx = (HX_VSF(key, tag_i) == TAG_INT)
            ? HX_VSF(key, int_val)
            : (int64_t)__hx_to_double(key);
    } else {
        idx = HX_INT(key);
    }
    return hexa_array_set(container, idx, val);
}

// Silent type check for struct method dispatch (codegen_c2 ImplBlock support).
// Returns 1 if v is a TAG_MAP carrying a "__type__" field equal to type_name,
// or TAG_VALSTRUCT (always type "Val" — rt 32-G flat struct).
// Uses hash lookup instead of linear scan.
int hexa_is_type(HexaVal v, const char* type_name) {
    if (HX_IS_VALSTRUCT(v)) {
        return type_name && strcmp(type_name, "Val") == 0;
    }
    if (!HX_IS_MAP(v) || !HX_MAP_TBL(v)) return 0;
    uint32_t h = hexa_fnv1a_str("__type__");
    int si = hmap_find(HX_MAP_TBL(v), "__type__", h);
    if (si < 0) return 0;
    HexaVal t = HX_MAP_TBL(v)->vals[si];
    return HX_IS_STR(t) && HX_STR(t) && strcmp(HX_STR(t), type_name) == 0;
}

// ══════════════════════════════════════════════════════════════
//  rt 32-G Phase 0: flat Val struct implementations
// ══════════════════════════════════════════════════════════════

// Redesigned constructor: every non-scalar arg is HexaVal-by-value so the
// payload (string s, array, map, valstruct pointer) is preserved verbatim.
// Scalars (tag/int/float/bool) still get coerced — those slots only ever
// see TAG_INT / TAG_FLOAT / TAG_BOOL in the interpreter.
HexaVal hexa_valstruct_new_v(
    HexaVal tag_v, HexaVal int_v, HexaVal float_v, HexaVal bool_v,
    HexaVal str_v, HexaVal char_v, HexaVal array_v,
    HexaVal fn_name_v, HexaVal fn_params_v, HexaVal fn_body_v,
    HexaVal struct_name_v, HexaVal struct_fields_v)
{
    // rt 32-L: the dominant fib RSS contributor — every Val{...} construction
    // in the interpreter (val_int / val_float / val_str / val_void / and ALL
    // user-side Val literals) ends up here. Arena-allocate when HEXA_VAL_ARENA
    // is on AND at least one scope mark is live (i.e. we're inside a fn call).
    // Module-init time allocations (cached singletons __VAL_VOID/__VAL_TRUE/
    // __VAL_INT_CACHE etc.) go through the heap path so the first scope pop
    // doesn't wipe them.
    // T33-fix-2: DISABLE arena for ValStruct. fn_arena_return only heapifies
    // the return value; globals (__VAL_INT_CACHE, env) escape unheapified.
    int from_arena = 0;
    HexaValStruct* s;
    if (from_arena) {
        s = (HexaValStruct*)hexa_val_arena_calloc(sizeof(HexaValStruct));
        s->from_arena = 1;
    } else {
        s = (HexaValStruct*)malloc(sizeof(HexaValStruct));
        if (!s) { fprintf(stderr, "OOM in valstruct_new_v\n"); exit(1); }
        s->from_arena = 0;
    }
    s->tag_i     = HX_IS_INT(tag_v) ? HX_INT(tag_v) : 0;
    s->int_val   = HX_IS_INT(int_v) ? HX_INT(int_v) : 0;
    s->float_val = HX_IS_FLOAT(float_v) ? HX_FLOAT(float_v) :
                   (HX_IS_INT(float_v) ? (double)HX_INT(float_v) : 0.0);
    s->bool_val  = HX_IS_BOOL(bool_v) ? HX_BOOL(bool_v) :
                   (HX_IS_INT(bool_v) ? (int)HX_INT(bool_v) : 0);
    // Polymorphic slots — store HexaVal verbatim. Closures / AST bodies /
    // struct field maps now survive Val construction intact.
    s->str_val       = str_v;
    s->char_val      = char_v;
    s->array_val     = array_v;
    s->fn_name       = fn_name_v;
    s->fn_params     = fn_params_v;
    s->fn_body       = fn_body_v;
    s->struct_name   = struct_name_v;
    s->struct_fields = struct_fields_v;
    HexaVal v = {.tag = TAG_VALSTRUCT};
    HX_SET_VS(v, s);
    return v;
}

// Helper: scalar accessors (tag/int/float/bool) still return primitive Vals.
// NOTE: hexa_valstruct_int kept — referenced internally by rt_write_bytes_v helpers.
HexaVal hexa_valstruct_int(HexaVal v) {
    if (!HX_IS_VALSTRUCT(v) || !HX_VS(v)) return hexa_int(0);
    return hexa_int(HX_VSF(v, int_val));
}

// Key-string dispatch — branches on key[0] to amortize strcmp.
// Polymorphic fields return raw HexaVal so closure bodies (TAG_MAP) and
// param lists (TAG_ARRAY) propagate through .fn_body / .fn_params reads.
HexaVal hexa_valstruct_get_by_key(HexaVal v, const char* key) {
    if (!HX_IS_VALSTRUCT(v) || !HX_VS(v)) {
        fprintf(stderr, "valstruct_get: not a TAG_VALSTRUCT\n");
        return hexa_void();
    }
    if (!key) return hexa_void();
    switch (key[0]) {
        case 't':
            if (!strcmp(key, "tag"))           return hexa_int(HX_VSF(v, tag_i));
            break;
        case 'i':
            if (!strcmp(key, "int_val"))       return hexa_int(HX_VSF(v, int_val));
            break;
        case 'f':
            if (!strcmp(key, "float_val"))     return hexa_float(HX_VSF(v, float_val));
            if (!strcmp(key, "fn_name"))       return HX_VSF(v, fn_name);
            if (!strcmp(key, "fn_params"))     return HX_VSF(v, fn_params);
            if (!strcmp(key, "fn_body"))       return HX_VSF(v, fn_body);
            break;
        case 'b':
            if (!strcmp(key, "bool_val"))      return hexa_bool(HX_VSF(v, bool_val));
            break;
        case 's':
            if (!strcmp(key, "str_val"))       return HX_VSF(v, str_val);
            if (!strcmp(key, "struct_name"))   return HX_VSF(v, struct_name);
            if (!strcmp(key, "struct_fields")) return HX_VSF(v, struct_fields);
            break;
        case 'c':
            if (!strcmp(key, "char_val"))      return HX_VSF(v, char_val);
            break;
        case 'a':
            if (!strcmp(key, "array_val"))     return HX_VSF(v, array_val);
            break;
        case '_':
            if (!strcmp(key, "__type__"))      return hexa_str("Val");
            break;
    }
    fprintf(stderr, "valstruct_get: unknown key '%s'\n", key);
    return hexa_void();
}

// Setter accepts any HexaVal payload — no TAG_STR gating, mirrors getter.
// 2026-04-17: COW — __VAL_INT_CACHE/float/bool singletons share ValStruct
// pointers. In-place mutation corrupts ALL users of that cache slot.
// Always shallow-copy before writing; old ValStruct leaked (no refcounting).
HexaVal hexa_valstruct_set_by_key(HexaVal v, const char* key, HexaVal val) {
    if (!HX_IS_VALSTRUCT(v) || !HX_VS(v)) {
        fprintf(stderr, "valstruct_set: not a TAG_VALSTRUCT\n");
        return v;
    }
    if (!key) return v;
    // COW: allocate a fresh copy
    HexaValStruct* orig = HX_VS(v);
    int use_arena = hexa_val_arena_on() && __hexa_val_mark_top > 0;
    HexaValStruct* cow;
    if (use_arena) {
        cow = (HexaValStruct*)hexa_val_arena_calloc(sizeof(HexaValStruct));
    } else {
        cow = (HexaValStruct*)malloc(sizeof(HexaValStruct));
        if (!cow) { fprintf(stderr, "OOM in valstruct COW\n"); exit(1); }
    }
    *cow = *orig;
    cow->from_arena = use_arena;
    HexaVal out = v;
    HX_SET_VS(out, cow);
    switch (key[0]) {
        case 't':
            if (!strcmp(key, "tag")) {
                cow->tag_i = HX_IS_INT(val) ? HX_INT(val) : cow->tag_i;
                return out;
            }
            break;
        case 'i':
            if (!strcmp(key, "int_val")) {
                cow->int_val = HX_IS_INT(val) ? HX_INT(val) : cow->int_val;
                return out;
            }
            break;
        case 'f':
            if (!strcmp(key, "float_val")) {
                if (HX_IS_FLOAT(val)) cow->float_val = HX_FLOAT(val);
                else if (HX_IS_INT(val)) cow->float_val = (double)HX_INT(val);
                return out;
            }
            if (!strcmp(key, "fn_name"))   { cow->fn_name   = val; return out; }
            if (!strcmp(key, "fn_params")) { cow->fn_params = val; return out; }
            if (!strcmp(key, "fn_body"))   { cow->fn_body   = val; return out; }
            break;
        case 'b':
            if (!strcmp(key, "bool_val")) {
                cow->bool_val = HX_IS_BOOL(val) ? HX_BOOL(val) :
                                 (HX_IS_INT(val) ? (int)HX_INT(val) : cow->bool_val);
                return out;
            }
            break;
        case 's':
            if (!strcmp(key, "str_val"))       { cow->str_val       = val; return out; }
            if (!strcmp(key, "struct_name"))   { cow->struct_name   = val; return out; }
            if (!strcmp(key, "struct_fields")) { cow->struct_fields = val; return out; }
            break;
        case 'c':
            if (!strcmp(key, "char_val"))  { cow->char_val  = val; return out; }
            break;
        case 'a':
            if (!strcmp(key, "array_val")) { cow->array_val = val; return out; }
            break;
    }
    fprintf(stderr, "valstruct_set: unknown key '%s'\n", key);
    return out;
}

// ── rt 32-D: scope-local arena allocator ─────────────────
// Problem: hexa_str_concat was doing `malloc(strlen(a)+strlen(b)+1)` per call
// with no free (no GC). d64 ML micro calls it 10^5+ times → heap fragmentation
// and accumulated RSS pressure even if individual strings are short-lived.
//
// Solution: linked-block bump allocator. Each hexa_arena_alloc() returns
// memory from the current block; when full, we chain a new block (default 4MB,
// or large enough for the request). `hexa_arena_mark()` + `hexa_arena_rewind()`
// give O(1) bulk-free semantics so a caller (env_push_scope / env_pop_scope or
// a future FFI binding) can dispose an entire scope's worth of string
// temporaries in one pointer reset.
//
// M4 16GB optimization: HEXA_STR_ARENA=1 by default (was OFF). The fn-arena
// scoping (STRUCTURAL-7: __hexa_fn_arena_enter/return) now handles escape
// analysis for return values, and hexa_val_heapify handles arena strings in
// maps/arrays/closures. Set HEXA_STR_ARENA=0 to disable if regressions appear.
//
// P2 scope (accepted partial): mark/rewind API + opt-in gate only. Hexa-level
// wiring into env_push_scope/env_pop_scope is a follow-up (requires
// hexa_full.hexa edits + interp rebuild, deferred per task description).

#define HEXA_ARENA_BLOCK_SIZE (4 * 1024 * 1024)   // 4MB default block

typedef struct HexaArenaBlock {
    struct HexaArenaBlock* next;   // NULL for last block
    size_t cap;                    // usable bytes in data[]
    size_t used;                   // bytes allocated from this block
    char   data[];                 // flexible; cap bytes follow the header
} HexaArenaBlock;

typedef struct {
    HexaArenaBlock* head;   // first block (never freed until hexa_arena_destroy)
    HexaArenaBlock* cur;    // current bump block
} HexaArena;

// Arena mark: captures current allocation frontier for O(1) rewind.
typedef struct {
    HexaArenaBlock* block;
    size_t used;
} HexaArenaMark;

static HexaArena __hexa_arena = {NULL, NULL};
static int __hexa_arena_enabled = -1;     // lazy env probe

static int hexa_arena_on(void) {
    if (__hexa_arena_enabled < 0) {
        const char* e = getenv("HEXA_STR_ARENA");
        // M4: default ON (was OFF). Env var HEXA_STR_ARENA=0 disables.
        __hexa_arena_enabled = (e && e[0] == '0') ? 0 : 1;
    }
    return __hexa_arena_enabled;
}

static HexaArenaBlock* hexa_arena_new_block(size_t min_cap) {
    size_t cap = min_cap > HEXA_ARENA_BLOCK_SIZE ? min_cap : HEXA_ARENA_BLOCK_SIZE;
    HexaArenaBlock* b = (HexaArenaBlock*)malloc(sizeof(HexaArenaBlock) + cap);
    if (!b) return NULL;
    b->next = NULL;
    b->cap = cap;
    b->used = 0;
    if (_hx_stats_on()) {
        _hx_stats_arena_blocks++;
        _hx_stats_arena_bytes += (int64_t)(sizeof(HexaArenaBlock) + cap);
        _hx_stats_sample_rss();
    }
    return b;
}

// Allocate `n` bytes from the arena, 8-byte aligned. Returns NULL on OOM.
// Public so future FFI consumers (env_push/pop_scope, parser temporaries)
// can opt in. Thread-safety: single-threaded runtime; no locks needed.
void* hexa_arena_alloc(size_t n) {
    // Round up to 8-byte alignment so we can safely return the pointer to
    // any caller that may store it where alignment matters.
    n = (n + 7u) & ~(size_t)7u;
    if (n == 0) n = 8;
    if (!__hexa_arena.head) {
        __hexa_arena.head = hexa_arena_new_block(n);
        __hexa_arena.cur = __hexa_arena.head;
        if (!__hexa_arena.head) return NULL;
    }
    HexaArenaBlock* b = __hexa_arena.cur;
    if (b->used + n > b->cap) {
        // Walk forward first: rewind may have left empty tail blocks we can reuse.
        HexaArenaBlock* nb = b->next;
        while (nb && nb->cap < n) nb = nb->next;
        if (!nb) {
            nb = hexa_arena_new_block(n);
            if (!nb) return NULL;
            // Append to end of chain (preserve any rewound-empty tail blocks).
            HexaArenaBlock* tail = __hexa_arena.cur;
            while (tail->next) tail = tail->next;
            tail->next = nb;
        }
        nb->used = 0;
        __hexa_arena.cur = nb;
        b = nb;
    }
    void* p = (void*)(b->data + b->used);
    b->used += n;
    if (_hx_stats_on()) _hx_stats_arena_alloc++;
    return p;
}

// Save current allocation frontier. Callers pair with hexa_arena_rewind.
HexaArenaMark hexa_arena_mark(void) {
    HexaArenaMark m;
    m.block = __hexa_arena.cur;
    m.used = __hexa_arena.cur ? __hexa_arena.cur->used : 0;
    return m;
}

// Rewind arena to a previously captured mark. O(1). Blocks after `mark.block`
// retain their backing memory (for reuse on next alloc) but are logically
// empty.
void hexa_arena_rewind(HexaArenaMark m) {
    if (!m.block) {
        if (__hexa_arena.head) {
            __hexa_arena.head->used = 0;
            __hexa_arena.cur = __hexa_arena.head;
        }
        return;
    }
    m.block->used = m.used;
    // Mark all subsequent blocks as empty (they stay linked, reusable).
    for (HexaArenaBlock* b = m.block->next; b; b = b->next) b->used = 0;
    __hexa_arena.cur = m.block;
}

// Rewind to beginning (keep blocks for reuse). Public for follow-up wiring.
void hexa_arena_reset(void) {
    if (!__hexa_arena.head) return;
    for (HexaArenaBlock* b = __hexa_arena.head; b; b = b->next) b->used = 0;
    __hexa_arena.cur = __hexa_arena.head;
}

// ── rt 32-L: Val arena scope stack + heapify ─────────────
// Goal: bump-allocate short-lived `Val { ... }` struct literals (the dominant
// HexaMapTable churn in fib-style recursive interpreted programs) and free
// them in O(1) per scope pop. Reuses the existing block chain from rt 32-D
// (hexa_arena_alloc) so str-concat and val-pack share a single bump frontier.
//
// Lifetime model: a per-scope mark stack mirrors env_push_scope / env_pop_scope
// in hexa_full.hexa. On scope pop, any Val that survives the scope (the fn
// return value, env_set targets in the parent scope, closure captures) MUST
// be heapified BEFORE the rewind so its backing storage moves to malloc. We
// expose mark/pop/heapify via the `env(...)` builtin path with reserved
// `__HEXA_ARENA_*` keys — no transpiler edit needed.
//
// Gate: HEXA_VAL_ARENA env var. Default ON as of S7-B (2026-04-16) —
// Phase A wired codegen_c2 __hexa_fn_arena_enter/return on all fn
// boundaries; T33 corruption fixed; full regression suite verified.
// Opt-out: HEXA_VAL_ARENA=0.
//
// T33 audit (2026-04-14): minimum repro still failing is array-passthrough
// through two nested user fns — e.g.
//     fn wrap(){return ["a","b","c"]}  fn touch(x){return x}
//     fn inner(){let l=wrap(); let z=touch(l); println(type_of(l[0]))}
// prints "map" under ARENA=1 (expect "str"). Symptom: array_store slot gets
// reclaimed (array_store[K] = []) even though `l` and `z` still hold it —
// consistent with a missed env_pop_scope decref path that operates on a
// stale-tag HexaValStruct after arena rewind. Canary test: T33b-on in
// tests/regression.hexa. Fix is blocked on the interp rebuild
// regression (codegen_c2 FnDecl + forward-decl issue; see task tracker).
// Keep default OFF until both the canary flips PASS and the module_loader
// use-path smoke (see ml_resolve heapify discussion above) goes green.

// Per-scope mark stack. Sized for deep recursion (fib(30) ≈ 30 frames; we
// budget 64K to cover ML stack depths). Overflow falls back to silently
// skipping the rewind for that scope (correctness preserved; arena just
// grows until the outer scope pops and rewinds).
#define HEXA_VAL_MARK_STACK_CAP 65536

typedef struct {
    HexaArenaBlock* block;
    size_t used;
} HexaValMark;

static HexaValMark __hexa_val_marks[HEXA_VAL_MARK_STACK_CAP];
int __hexa_val_mark_top = 0;            // count of pushed marks (extern-visible)
int __hexa_val_force_heap = 0;          // when set, valstruct_new_v uses heap (codegen refs this)
static int __hexa_val_arena_enabled = -1;  // -1 = lazy probe

static int hexa_val_arena_on(void) {
    if (__hexa_val_arena_enabled < 0) {
        const char* e = getenv("HEXA_VAL_ARENA");
        // S7-B: default ON (2026-04-16). Phase A wired codegen_c2
        // __hexa_fn_arena_enter/return; T33 corruption fixed; full 236-example
        // + 16-case dispatch regression suite passed 0-regression under ARENA=1.
        // Opt-out: HEXA_VAL_ARENA=0.
        if (!e || !e[0]) {
            __hexa_val_arena_enabled = 1;
        } else {
            __hexa_val_arena_enabled = (e[0] == '1' || e[0] == 'y' || e[0] == 'Y') ? 1 : 0;
        }
    }
    return __hexa_val_arena_enabled;
}

// Zeroed allocation from the bump arena. Falls back to calloc on OOM so the
// caller never sees a NULL — keeps the hot path branch-free and correct.
static void* hexa_val_arena_calloc(size_t n) {
    void* p = hexa_arena_alloc(n);
    if (!p) return calloc(1, n);
    memset(p, 0, n);
    return p;
}

// Push current arena frontier as a scope mark. Called on env_push_scope via
// the env("__HEXA_ARENA_PUSH__") interception in hexa_env_var. Saturating: if
// the mark stack is full, we silently no-op rather than crashing.
static void hexa_val_arena_scope_push(void) {
    if (__hexa_val_mark_top >= HEXA_VAL_MARK_STACK_CAP) return;
    HexaValMark* m = &__hexa_val_marks[__hexa_val_mark_top++];
    m->block = __hexa_arena.cur;
    m->used = __hexa_arena.cur ? __hexa_arena.cur->used : 0;
}

// ω-interp-1 (2026-04-26): per-frame "dirty" flag — gates the expensive T33
// Fix 4 walk over array_store[idx] in hexa_val_heapify. Set whenever a push
// (or any other path that may store an arena-allocated child Val into a
// slot's items buffer) occurs inside an active arena frame. Cleared on
// scope_push so each frame starts clean.
//
// Without this, every fn-arena return for a TAG_ARRAY interpreter Val
// triggered the slot walk unconditionally, turning tight push loops
// (anima_audio.hexa vocal_hexa() — 4800 .push() calls per call) into O(N²).
// Repro: /tmp/test_push_n.hexa.
static int __hexa_arena_slot_dirty = 0;

// Pop scope mark and rewind arena to it. Caller must have heapified any Val
// that escapes the popped scope BEFORE calling this. If the stack is empty
// (under-pop), no-op (defensive).
//
// rt 32-M: array arena currently fires only for slice_fast (lifetime-safe
// by construction — the slice is consumed by call_user_fn's args parameter
// and never stored persistently). Push-arena (first-push into arena) was
// prototyped + reverted; see hexa_array_push comment for the Hexa
// pass-by-value / snapshot-ABA blocker.
static void hexa_val_arena_scope_pop(void) {
    if (__hexa_val_mark_top <= 0) return;
    HexaValMark m = __hexa_val_marks[--__hexa_val_mark_top];
    if (!m.block) {
        // Mark predates any allocation — full reset.
        if (__hexa_arena.head) {
            for (HexaArenaBlock* b = __hexa_arena.head; b; b = b->next) b->used = 0;
            __hexa_arena.cur = __hexa_arena.head;
        }
        return;
    }
    m.block->used = m.used;
    for (HexaArenaBlock* b = m.block->next; b; b = b->next) b->used = 0;
    __hexa_arena.cur = m.block;
}

// Forward decl — heapify itself is recursive over nested arena maps.
HexaVal hexa_val_heapify(HexaVal v);

// T33 Fix 4: weak-link to the interpreter's array_store / map_store globals.
// These are emitted as `HexaVal array_store;` / `HexaVal map_store;` by the
// codegen for `pub let mut array_store = []` in interpreter.hexa. They are
// host-array TAG_ARRAY HexaVals that hold the *contents* of every interpreter
// array / map (the wrapped HexaValStruct only carries a slot index in
// vs->int_val). Because hexa_val_heapify previously stopped at the index-
// holding VALSTRUCT, arena-allocated child elements stored *inside*
// array_store[idx] / map_store[idx] were never deep-copied — when the arena
// rewound on scope-pop, those elements turned into dangling memory that
// later prints as "index 5702266960 out of bounds" or as the wrong tag.
//
// We weak-link so this object can also link into translation units that
// don't include hexa_full.hexa (e.g. the bootstrap_compiler.c stub or
// standalone test harnesses). At runtime we NULL-check before deref.
extern HexaVal array_store  __attribute__((weak_import, weak));
extern HexaVal map_store    __attribute__((weak_import, weak));
extern HexaVal struct_store __attribute__((weak_import, weak));
__attribute__((weak)) HexaVal array_store  __attribute__((common));
__attribute__((weak)) HexaVal map_store    __attribute__((common));
__attribute__((weak)) HexaVal struct_store __attribute__((common));

// Interpreter-level Val.tag constants (see self/interpreter.hexa lines 20-30).
// Hardcoded here because the hexa-side `let TAG_ARRAY = 5` is emitted as a
// HexaVal, not a C #define. Mismatch with runtime.c's HexaTag enum is
// intentional — these are *interpreter Val tags* (vs->tag_i), not the
// outer HexaVal.tag.
#define HEXA_INTERP_TAG_ARRAY  5
#define HEXA_INTERP_TAG_STRUCT 7
#define HEXA_INTERP_TAG_MAP    10

// Deep-copy an arena-allocated HexaMapTable to malloc'd storage. Recursive
// over nested values (TAG_MAP and TAG_ARRAY descend; scalars + already-heap
// strings are bit-copied; TAG_STR with arena-pointing s gets strdup'd).
static HexaMapTable* hmap_heapify(HexaMapTable* src) {
    if (!src) return NULL;
    HexaMapTable* dst = hmap_alloc_ex(src->ht_cap, 0);
    // Re-insert by walking the order arrays so insertion order survives the
    // copy. The destination is freshly empty; we use the same hash for direct
    // slot placement (no rehash necessary because ht_cap matches).
    uint32_t mask = (uint32_t)(dst->ht_cap - 1);
    if (dst->order_cap < src->order_cap) {
        // Grow order arrays to match source capacity (dst->order_cap was
        // sized off ht_cap; src may have grown beyond that via push paths).
        dst->order_keys = (char**)realloc(dst->order_keys, sizeof(char*) * src->order_cap);
        dst->order_vals = (HexaVal*)realloc(dst->order_vals, sizeof(HexaVal) * src->order_cap);
        dst->order_cap = src->order_cap;
    }
    for (int i = 0; i < src->len; i++) {
        const char* k = src->order_keys[i];
        if (!k) continue;
        uint32_t h = hexa_fnv1a_str(k);
        uint32_t idx = h & mask;
        while (dst->slots[idx].key) idx = (idx + 1) & mask;
        dst->slots[idx].key = strdup(k);
        dst->slots[idx].hash = h;
        dst->slots[idx].order_idx = dst->len;  // ROI-24
        HexaVal nv = hexa_val_heapify(src->order_vals[i]);
        dst->vals[idx] = nv;
        dst->order_keys[dst->len] = dst->slots[idx].key;
        dst->order_vals[dst->len] = nv;
        dst->len++;
    }
    return dst;
}

// Public: heapify a HexaVal. Idempotent on already-heap values. Recursively
// processes nested TAG_MAP / TAG_ARRAY / TAG_STR payloads. Scalar tags
// (INT/FLOAT/BOOL/VOID) are returned as-is. TAG_FN / TAG_VALSTRUCT / TAG_CLOSURE
// retain their pointer identity (those allocators don't currently use the
// arena, so heapify is a no-op for them).
// Valid heap/arena pointers on macOS/Linux live well above the NULL page.
// Any pointer under 0x1000 is a sign of corrupted Val slot (small-int bleed
// through the union, uninitialized stack, or freed memory with zeroed bits).
// Treat as no-op rather than dereferencing garbage.
// 2026-04-17: codegen_c2_extended driver repro produced recursive heapify
// calls with v.arr_ptr / v.vs / v.map_ptr = 0x0E causing SIGSEGV at offset
// +504 of hexa_val_heapify. Root cause is upstream Val corruption but the
// fault mode (sub-page deref) is easy to guard here.
#define HX_PTR_OK(p) ((uintptr_t)(p) >= 0x1000)

HexaVal hexa_val_heapify(HexaVal v) {
    switch (HX_TAG(v)) {
        case TAG_MAP:
            if (HX_MAP_TBL(v) && HX_MAP_TBL(v)->from_arena) {
                HexaMapTable* heap_tbl = hmap_heapify(HX_MAP_TBL(v));
                HX_SET_MAP_TBL(v, heap_tbl);
                HX_SET_MAP_LEN(v, heap_tbl ? heap_tbl->len : 0);
            } else if (HX_MAP_TBL(v)) {
                // Heap table — but its values may include arena maps. Walk.
                for (int i = 0; i < HX_MAP_TBL(v)->len; i++) {
                    HX_MAP_TBL(v)->order_vals[i] = hexa_val_heapify(HX_MAP_TBL(v)->order_vals[i]);
                    // Also fix the hash slot's mirror so later lookups see the heap copy.
                    int si = hmap_find(HX_MAP_TBL(v), HX_MAP_TBL(v)->order_keys[i],
                                       hexa_fnv1a_str(HX_MAP_TBL(v)->order_keys[i]));
                    if (si >= 0) HX_MAP_TBL(v)->vals[si] = HX_MAP_TBL(v)->order_vals[i];
                }
            }
            return v;
        case TAG_ARRAY: {
            // rt 32-M: if cap<0, items buffer is arena-backed — deep-copy to
            // malloc before the rewind. Then heapify nested elements (rt 32-L
            // handles nested arena maps / valstructs / strings).
            if (HX_ARR_CAP(v) < 0 && HX_ARR_ITEMS(v)) {
                if (_hx_stats_on()) _hx_stats_array_arena_heapify++;
                int real_cap = -HX_ARR_CAP(v);
                HexaVal* heap = hexa_array_promote_to_heap(HX_ARR_ITEMS(v), HX_ARR_LEN(v), real_cap);
                if (heap) {
                    HX_SET_ARR_ITEMS(v, heap);
                    HX_SET_ARR_CAP(v, real_cap);  // positive → heap
                }
                // On OOM we leave the arena pointer; caller may see invalid
                // memory after rewind — best-effort semantics match Val arena.
            }
            for (int i = 0; i < HX_ARR_LEN(v); i++) {
                HX_ARR_ITEMS(v)[i] = hexa_val_heapify(HX_ARR_ITEMS(v)[i]);
            }
            return v;
        }
        case TAG_STR: {
            // Conservative: if the arena owns the string (i.e. between any
            // arena block start and frontier), strdup it. Detection via
            // pointer-range check across active blocks.
            if (!HX_STR(v)) return v;
            for (HexaArenaBlock* b = __hexa_arena.head; b; b = b->next) {
                char* base = b->data;
                char* end = base + b->cap;
                if (HX_STR(v) >= base && HX_STR(v) < end) {
                    HX_SET_STR(v, strdup(HX_STR(v)));
                    if (_hx_stats_on()) _hx_stats_str_arena_heapify++;
                    break;
                }
            }
            return v;
        }
        case TAG_CLOSURE: {
            // env_box itself is malloc'd in hexa_closure_new — never arena.
            // But the captured array stored inside it may transitively contain
            // arena maps (params/body/captures). Recurse into env_box's value.
            if (HX_CLO_ENV(v)) {
                *HX_CLO_ENV(v) = hexa_val_heapify(*HX_CLO_ENV(v));
            }
            return v;
        }
        case TAG_VALSTRUCT: {
            // rt 32-L: deep-copy arena-allocated HexaValStruct (the dominant
            // fib hot path) to malloc'd storage. Polymorphic field slots are
            // recursively heapified — closure bodies / param arrays / nested
            // structs all need the same treatment.
            if (!HX_PTR_OK(HX_VS(v))) return v;
            if (HX_VSF(v, from_arena)) {
                HexaValStruct* dst = (HexaValStruct*)malloc(sizeof(HexaValStruct));
                if (!dst) return v;  // OOM — caller will see arena pointer (best-effort)
                dst->tag_i        = HX_VSF(v, tag_i);
                dst->int_val      = HX_VSF(v, int_val);
                dst->float_val    = HX_VSF(v, float_val);
                dst->bool_val     = HX_VSF(v, bool_val);
                dst->from_arena   = 0;
                dst->str_val      = hexa_val_heapify(HX_VSF(v, str_val));
                dst->char_val     = hexa_val_heapify(HX_VSF(v, char_val));
                dst->array_val    = hexa_val_heapify(HX_VSF(v, array_val));
                dst->fn_name      = hexa_val_heapify(HX_VSF(v, fn_name));
                dst->fn_params    = hexa_val_heapify(HX_VSF(v, fn_params));
                dst->fn_body      = hexa_val_heapify(HX_VSF(v, fn_body));
                dst->struct_name  = hexa_val_heapify(HX_VSF(v, struct_name));
                dst->struct_fields= hexa_val_heapify(HX_VSF(v, struct_fields));
                HX_SET_VS(v, dst);
            } else {
                // Heap valstruct may still hold arena children (e.g. an arena
                // string was assigned to .str_val). Recurse for safety.
                // 2026-04-17 COW: __VAL_INT_CACHE singletons are heap valstructs
                // shared across the program. In-place mutation corrupts the cache.
                // Shallow-copy before heapifying children.
                HexaValStruct* hcow = (HexaValStruct*)malloc(sizeof(HexaValStruct));
                if (hcow) {
                    *hcow = *HX_VS(v);
                    hcow->from_arena = 0;
                    hcow->str_val      = hexa_val_heapify(hcow->str_val);
                    hcow->char_val     = hexa_val_heapify(hcow->char_val);
                    hcow->array_val    = hexa_val_heapify(hcow->array_val);
                    hcow->fn_name      = hexa_val_heapify(hcow->fn_name);
                    hcow->fn_params    = hexa_val_heapify(hcow->fn_params);
                    hcow->fn_body      = hexa_val_heapify(hcow->fn_body);
                    hcow->struct_name  = hexa_val_heapify(hcow->struct_name);
                    hcow->struct_fields= hexa_val_heapify(hcow->struct_fields);
                    HX_SET_VS(v, hcow);
                }
            }
            // T33 Fix 4: when the interpreter Val is a TAG_ARRAY (5) or
            // TAG_MAP (10), its contents live in array_store / map_store
            // indexed by vs->int_val — NOT in the VALSTRUCT itself. Heapify
            // the host array slot so each element survives arena rewind.
            //
            // We only descend if the corresponding global symbol is linked
            // (weak_import: &foo == NULL when absent — e.g. bootstrap stub).
            // Bounds-check against the host array length to defend against
            // freed / reused slot indices observed during T33 repro.
            if (HX_VSF(v, tag_i) == HEXA_INTERP_TAG_ARRAY && &array_store != 0) {
                if (HX_IS_ARRAY(array_store) && HX_ARR_ITEMS(array_store)) {
                    int64_t idx = HX_VSF(v, int_val);
                    if (idx >= 0 && idx < (int64_t)HX_ARR_LEN(array_store)) {
                        HX_ARR_ITEMS(array_store)[idx] =
                            hexa_val_heapify(HX_ARR_ITEMS(array_store)[idx]);
                    }
                }
            } else if (HX_VSF(v, tag_i) == HEXA_INTERP_TAG_MAP && &map_store != 0) {
                if (HX_IS_ARRAY(map_store) && HX_ARR_ITEMS(map_store)) {
                    int64_t idx = HX_VSF(v, int_val);
                    if (idx >= 0 && idx < (int64_t)HX_ARR_LEN(map_store)) {
                        // map_store[idx] is a TAG_ARRAY of length 2:
                        // [keys_array, values_array]. Heapifying the outer
                        // array walks both keys and values transitively.
                        HX_ARR_ITEMS(map_store)[idx] =
                            hexa_val_heapify(HX_ARR_ITEMS(map_store)[idx]);
                    }
                }
            } else if (HX_VSF(v, tag_i) == HEXA_INTERP_TAG_STRUCT && &struct_store != 0) {
                // rt 36-D: struct_store[idx] is a TAG_MAP (field map).
                // Heapify it so closure-captured structs survive scope pop.
                if (HX_IS_ARRAY(struct_store) && HX_ARR_ITEMS(struct_store)) {
                    int64_t idx = HX_VSF(v, int_val);
                    if (idx >= 0 && idx < (int64_t)HX_ARR_LEN(struct_store)) {
                        HX_ARR_ITEMS(struct_store)[idx] =
                            hexa_val_heapify(HX_ARR_ITEMS(struct_store)[idx]);
                    }
                }
            }
            return v;
        }
        default:
            return v;
    }
}

// Public C entry — exposed for the env("__HEXA_ARENA_HEAPIFY_RETURN__") path.
// Heapifies the global return_val (a hexa_full.hexa pub let mut, emitted as a
// C global). Declared extern; the symbol is provided by the generated C from
// hexa_full.hexa. Guarded by a weak-link check at call time so this object can
// link standalone (e.g. test harnesses that don't include hexa_full.hexa).
// rt#32-N build-fix: on macOS arm64 clang drops the weak attribute through
// plain extern; add weak_import so unresolved → NULL at runtime. Also provide
// a weak fallback definition so programs without a global `return_val` (like
// hexa_cc.c's own build) still link. When hexa_full.hexa's generated C
// defines `HexaVal return_val;` later, the strong definition wins (weak
// resolves to the strong one). A duplicate-weak -Wignored-attributes warning
// is harmless.
extern HexaVal return_val __attribute__((weak_import, weak));
__attribute__((weak)) HexaVal return_val __attribute__((common));
static void hexa_val_arena_heapify_return(void) {
    if (&return_val) {
        return_val = hexa_val_heapify(return_val);
    }
}

// ── STRUCTURAL-7 Phase A: codegen fn-boundary arena enter/return ──
// Implementations for the forward declarations near line 579. When the arena
// is OFF (HEXA_VAL_ARENA=0), both are no-ops. When ON (default since S7-B),
// enter() pushes an arena scope mark and return() heapifies the return value
// (promoting any arena-allocated memory to the heap) then pops the scope.
void __hexa_fn_arena_enter(void) {
    if (!hexa_val_arena_on()) return;
    hexa_val_arena_scope_push();
}

HexaVal __hexa_fn_arena_return(HexaVal ret) {
    if (!hexa_val_arena_on()) return ret;
    if (__hexa_val_mark_top <= 0) return ret;  // no matching push — defensive
    ret = hexa_val_heapify(ret);
    hexa_val_arena_scope_pop();
    return ret;
}

// ── String operations ────────────────────────────────────

// rt 32-D: optional arena path. Under HEXA_STR_ARENA=1, temporary concat
// results are bump-allocated from the scope arena instead of malloc'd
// individually. Safety: strings that escape the arena's lifetime (returns,
// map inserts, closure captures) must be copied into the persistent heap
// by the caller — for now we do NOT perform automatic escape analysis, so
// the gate defaults to OFF. With the gate OFF behavior is bit-identical to
// pre-rt-32-D (every concat still does its own malloc).
HexaVal hexa_str_concat(HexaVal a, HexaVal b) {
    if (_hx_stats_on()) _hx_stats_str_concat++;
    char* sa = HX_IS_STR(a) ? HX_STR(a) : "";
    char* sb = HX_IS_STR(b) ? HX_STR(b) : "";
    size_t la = strlen(sa);
    size_t lb = strlen(sb);
    // ROI-36: empty-string elision — skip alloc+memcpy when one side is "".
    if (la == 0) return HX_IS_STR(b) ? b : hexa_str(sb);
    if (lb == 0) return HX_IS_STR(a) ? a : hexa_str(sa);
    size_t total = la + lb + 1;
    char* result;
    if (hexa_arena_on()) {
        result = (char*)hexa_arena_alloc(total);
        if (!result) {
            // Fallback: arena OOM → heap malloc so we stay correct.
            result = (char*)malloc(total);
        } else {
            if (_hx_stats_on()) _hx_stats_str_concat_arena++;
        }
    } else {
        result = (char*)malloc(total);
    }
    // memcpy avoids the strcpy+strcat double walk.
    memcpy(result, sa, la);
    memcpy(result + la, sb, lb);
    result[la + lb] = '\0';
    return hexa_str_own(result);
}

// L4 ω-cycle (lossless) — byte-index scan ASCII fast-path.
//
// Original: 1 hexa_str(buf) intern call per byte → FNV1a hash + linear probe
// + (first-time) strdup. For an N-byte source the lexer's hot path
// (`chars = source.chars()`) does N intern lookups even when all 256 single-
// byte values are already cached. On 842KB ai_native_pass.hexa baseline that
// is ~840k FNV1a calls + ~840k array_push calls inside parse phase.
//
// New: lazy-init a 256-entry HexaVal cache for single-byte strings, and have
// hexa_str_chars do a direct table lookup per byte. Output is byte-equivalent
// (same interned pointer that hexa_str(buf) would have returned anyway, since
// 1-byte strings hash into the intern table the same way), so token stream is
// preserved bitwise.
//
// Lossless axes:
//   performance: hexa_str(buf) result identity is preserved (same intern
//                pointer → same hexa_eq behavior).
//   resource:    +256*sizeof(HexaVal) static = ~4KB BSS one-shot.
//   speed:       O(1) table lookup vs hash + probe.
static HexaVal _cached_byte_str[256];
static int     _cached_byte_str_ready = 0;

static void _hexa_init_byte_str_cache(void) {
    if (_cached_byte_str_ready) return;
    char buf[2];
    buf[1] = '\0';
    for (int i = 0; i < 256; i++) {
        buf[0] = (char)(unsigned char)i;
        // For NUL byte we still produce the canonical empty-string interned
        // value so hexa_eq matches whatever hexa_str("") returns elsewhere.
        _cached_byte_str[i] = hexa_str(buf);
    }
    _cached_byte_str_ready = 1;
}

HexaVal hexa_str_chars(HexaVal s) {
    HexaVal arr = hexa_array_new();
    if (!HX_IS_STR(s)) return arr;
    if (!_cached_byte_str_ready) _hexa_init_byte_str_cache();
    const unsigned char* p = (const unsigned char*)HX_STR(s);
    for (; *p; p++) {
        arr = hexa_array_push(arr, _cached_byte_str[*p]);
    }
    return arr;
}

int hexa_str_contains(HexaVal s, HexaVal sub) {
    return strstr(HX_STR(s), HX_STR(sub)) != NULL;
}

int hexa_str_eq(HexaVal a, HexaVal b) {
    if (!HX_IS_STR(a) || !HX_IS_STR(b)) return 0;
    // Optimization #11: interned strings share pointers
    if (HX_STR(a) == HX_STR(b)) return 1;
    return strcmp(HX_STR(a), HX_STR(b)) == 0;
}

// M1 full · str_ext Step 5 (hxa-20260423-003): rt_str_starts_with/ends_with —
// codegen emits rt_str_* directly; hexa_str_starts_with/ends_with shims retired.
int rt_str_starts_with(HexaVal s, HexaVal prefix) {
    if (!HX_IS_STR(s) || !HX_IS_STR(prefix)) return 0;
    size_t plen = strlen(HX_STR(prefix));
    return strncmp(HX_STR(s), HX_STR(prefix), plen) == 0;
}

int rt_str_ends_with(HexaVal s, HexaVal suffix) {
    if (!HX_IS_STR(s) || !HX_IS_STR(suffix)) return 0;
    size_t slen = strlen(HX_STR(s));
    size_t sfxlen = strlen(HX_STR(suffix));
    if (sfxlen > slen) return 0;
    return strcmp(HX_STR(s) + slen - sfxlen, HX_STR(suffix)) == 0;
}

HexaVal hexa_str_substring(HexaVal s, HexaVal start, HexaVal end) {
    if (!HX_IS_STR(s)) return hexa_str("");
    int64_t slen = strlen(HX_STR(s));
    int64_t a = HX_INT(start), b = HX_INT(end);
    if (a < 0) a = 0;
    if (b > slen) b = slen;
    if (a >= b) return hexa_str("");
    char* buf = (char*)malloc(b - a + 1);
    memcpy(buf, HX_STR(s) + a, b - a);
    buf[b - a] = '\0';
    return hexa_str_own(buf);
}

int64_t hexa_str_index_of(HexaVal s, HexaVal sub) {
    if (!HX_IS_STR(s) || !HX_IS_STR(sub)) return -1;
    char* p = strstr(HX_STR(s), HX_STR(sub));
    if (!p) return -1;
    return (int64_t)(p - HX_STR(s));
}

// `.index_of(sub, start)` — first occurrence at-or-after byte offset `start`.
// hxa-20260423-012: the 2-arg form was silently dropping `start`, forcing
// anima to emit fields in a hack order (rank BEFORE weights) to dodge the
// miscompare. Semantics: clamp start to [0,len]; empty needle → start.
int64_t hexa_str_index_of_from(HexaVal s, HexaVal sub, HexaVal start) {
    if (!HX_IS_STR(s) || !HX_IS_STR(sub)) return -1;
    const char* hay = HX_STR(s);
    size_t hlen = strlen(hay);
    int64_t st = HX_INT(start);
    if (st < 0) st = 0;
    if ((size_t)st > hlen) return -1;
    const char* needle = HX_STR(sub);
    if (*needle == '\0') return st;
    const char* p = strstr(hay + st, needle);
    if (!p) return -1;
    return (int64_t)(p - hay);
}

// Returns byte offset of LAST occurrence of `sub` within `s`, or -1.
// Mirrors interpreter `.rfind`/`.last_index_of` at self/hexa_full.hexa:15741.
// Added 2026-04-23 (hxa-20260422-002 lang_gap prio=95, rfind blocker).
int64_t hexa_str_last_index_of(HexaVal s, HexaVal sub) {
    if (!HX_IS_STR(s) || !HX_IS_STR(sub)) return -1;
    const char* hay = HX_STR(s);
    const char* needle = HX_STR(sub);
    size_t nlen = strlen(needle);
    size_t hlen = strlen(hay);
    if (nlen == 0) return (int64_t)hlen;
    if (nlen > hlen) return -1;
    int64_t last = -1;
    const char* p = hay;
    while ((p = strstr(p, needle)) != NULL) {
        last = (int64_t)(p - hay);
        p += 1;  // overlap-safe
    }
    return last;
}

// Byte-indexed single-char extraction. `.char_at(i)` → 1-byte string at
// offset i, empty string if out-of-range. Matches the byte-orientation
// of every other hexa_str_* helper (runtime has no UTF-8 codepoint iter
// — strings are treated as uninterpreted byte sequences, with the
// exception of UTF-8-safe display in hexa_println).
HexaVal hexa_str_char_at(HexaVal s, HexaVal idx) {
    if (!HX_IS_STR(s)) return hexa_str("");
    int64_t i = HX_INT(idx);
    int64_t n = (int64_t)strlen(HX_STR(s));
    // Python-style negative index — align interp `s[-1]` 지원 (`c` 반환).
    if (i < 0) i += n;
    if (i < 0 || i >= n) return hexa_str("");
    char buf[2] = { HX_STR(s)[i], '\0' };
    return hexa_str(buf);
}

// Byte value at offset i (0..255), or -1 if out-of-range. JS-analog
// `.char_code_at` expects UTF-16 code units; we expose raw byte
// values since hexa strings are byte-sequenced. A later UTF-8
// codepoint API (`.code_point_at`) can layer on if needed.
HexaVal hexa_str_char_code_at(HexaVal s, HexaVal idx) {
    if (!HX_IS_STR(s)) return hexa_int(-1);
    int64_t i = HX_INT(idx);
    int64_t n = (int64_t)strlen(HX_STR(s));
    if (i < 0) i += n;  // 음수 wraparound — char_at 과 align
    if (i < 0 || i >= n) return hexa_int(-1);
    return hexa_int((int64_t)(unsigned char)HX_STR(s)[i]);
}

// ── Array operations ─────────────────────────────────
HexaVal hexa_array_pop(HexaVal arr) {
    if (!HX_IS_ARRAY(arr) || HX_ARR_LEN(arr) == 0) return hexa_void();
    return HX_ARR_ITEMS(arr)[HX_ARR_LEN(arr) - 1];
}

HexaVal hexa_array_reverse(HexaVal arr) {
    // Polymorphic — string.reverse() / array.reverse() share codegen emit.
    // Previously fell through for strings ("abc".reverse() → "abc" no-op)
    // because the `!HX_IS_ARRAY` guard returned early. Interp handled it
    // via Val-level byte reverse; AOT was silently wrong.
    if (HX_IS_STR(arr)) {
        const char* s = HX_STR(arr);
        size_t n = strlen(s);
        char* buf = (char*)malloc(n + 1);
        for (size_t i = 0; i < n; i++) buf[i] = s[n - 1 - i];
        buf[n] = '\0';
        return hexa_str_own(buf);
    }
    if (!HX_IS_ARRAY(arr)) return arr;
    HexaVal result = hexa_array_new();
    for (int i = HX_ARR_LEN(arr) - 1; i >= 0; i--)
        result = hexa_array_push(result, HX_ARR_ITEMS(arr)[i]);
    return result;
}

static int hexa_sort_cmp(const void* a, const void* b) {
    HexaVal va = *(const HexaVal*)a, vb = *(const HexaVal*)b;
    if (HX_IS_INT(va) && HX_IS_INT(vb)) return HX_INT(va) < HX_INT(vb) ? -1 : HX_INT(va) > HX_INT(vb) ? 1 : 0;
    if (HX_IS_FLOAT(va) && HX_IS_FLOAT(vb)) return HX_FLOAT(va) < HX_FLOAT(vb) ? -1 : HX_FLOAT(va) > HX_FLOAT(vb) ? 1 : 0;
    return 0;
}

HexaVal hexa_array_sort(HexaVal arr) {
    // Previously `result = arr` copied the HexaVal struct — but both still
    // alias array_store[arr.int_val]. `HX_SET_ARR_ITEMS(result, malloc(...))`
    // therefore also replaced arr's items pointer → the following
    // `memcpy(HX_ARR_ITEMS(result), HX_ARR_ITEMS(arr), ...)` memcpy'd the
    // brand-new empty buf onto itself. qsort'd that garbage → caller saw
    // [0,0,0]. **Both** `r` and original `arr` came out zeroed.
    // Fix: build a fresh result via hexa_array_new + push (mirrors
    // hexa_array_reverse), then qsort the fresh buffer.
    if (!HX_IS_ARRAY(arr)) return arr;
    HexaVal result = hexa_array_new();
    int n = HX_ARR_LEN(arr);
    for (int i = 0; i < n; i++) {
        result = hexa_array_push(result, HX_ARR_ITEMS(arr)[i]);
    }
    qsort(HX_ARR_ITEMS(result), HX_ARR_LEN(result), sizeof(HexaVal), hexa_sort_cmp);
    return result;
}

// ── Exec ────────────────────────────────────────────
// 2026-04-26 cross-repo upstream (hive→hexa-lang): switched the read primitive
// from fgets()+strlen(buf) to fread(). Old loop computed chunk length via
// strlen() on the buffer fgets() filled — when the child wrote an embedded
// NUL byte (e.g. `git log --format=...%x00` for record boundaries),
// strlen(buf) returned 0 for that chunk's tail and every byte from the NUL
// onward inside that 4 KiB read window was silently dropped. The new loop
// uses fread() which reports the actual byte count, so the captured buffer
// faithfully retains every byte including embedded NULs.
//
// Caveat (string ABI): HexaVal strings are stored as a bare `char*` (see the
// HX_STR macro) and most downstream consumers (hexa_len → strlen, the intern
// table, str_eq → strcmp) still treat them as C-NUL-terminated. Hexa-source
// code that calls len() / == on the captured string still sees the C-string
// view (i.e. truncation at the first NUL). Callers that need full byte-level
// access to popen output with embedded NULs should use a byte-array variant
// modeled after rt_read_file_bytes (planned: `exec_bytes`). This fix is the
// read-loop slice of the problem — the buffer itself is now correct end-to-
// end, removing the runtime-layer truncation cliff.
HexaVal hexa_exec(HexaVal cmd) {
    if (!HX_IS_STR(cmd)) return hexa_str("");
    FILE* fp = popen(HX_STR(cmd), "r");
    if (!fp) return hexa_str("");
    char buf[4096]; size_t total = 0;
    size_t cap = 4096;
    char* result = (char*)malloc(cap); result[0] = '\0';
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        // +1 for the trailing NUL we always append so HX_STR() remains a
        // valid C-string for ABI consumers (see caveat above).
        while (total + n + 1 > cap) { cap *= 2; result = (char*)realloc(result, cap); }
        memcpy(result + total, buf, n);
        total += n;
    }
    result[total] = '\0';
    pclose(fp);
    return hexa_str_own(result);
}

// hexa_exec_stream — like hexa_exec, but invokes `on_line(line)` for each
// stdout line as it is read instead of buffering the entire output. Returns
// the child's exit code (same convention as hexa_exec_with_status[1]: 0..255
// for normal exit, 128+sig for signal, -1 for unknown).
//
// Rationale (2026-04-25): hexa_exec / hexa_exec_with_status both popen() the
// command and only return after pclose(). For long-running build/test
// commands that emit progress on stdout (npm test, cargo build, hexa-lang's
// own dispatch self-compile) the agent layer sees zero output until the child
// exits, which surfaces to the user as a multi-minute silent stall. Adding
// streaming lets the calling hexa code emit progress in real time without
// the runtime buffering multi-megabyte transcripts.
//
// Each line passed to the callback retains its trailing newline (as fgets
// returns it), except the final partial line if the child exited mid-line.
// Callback is invoked inline on the hexa runtime stack via hexa_call1 — the
// caller's hexa function executes synchronously between fgets() iterations.
HexaVal hexa_exec_stream_impl(HexaVal cmd, HexaVal on_line) {
    if (!HX_IS_STR(cmd)) return hexa_int(127);
    if (!HX_IS_FN(on_line) && !HX_IS_CLOSURE(on_line)) return hexa_int(127);
    FILE* fp = popen(HX_STR(cmd), "r");
    if (!fp) return hexa_int(127);
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp)) {
        // Construct a fresh hexa string per line (the buffer is reused).
        // hexa_str() copies, so this is safe across iterations.
        hexa_call1(on_line, hexa_str(buf));
    }
    int rc = pclose(fp);
    int exit_code;
    if (WIFEXITED(rc))      exit_code = WEXITSTATUS(rc);
    else if (WIFSIGNALED(rc)) exit_code = 128 + WTERMSIG(rc);
    else                     exit_code = -1;
    return hexa_int(exit_code);
}

// Forward decl + auto-wrap macro shim. The current hexa_v2 codegen emits
// `hexa_exec_stream(cmd, ident)` where `ident` is a raw C function symbol
// (e.g. `on_line`) rather than a HexaVal. To bridge that without rebuilding
// hexa_v2, we redirect the user-visible call through a wrapping macro that
// invokes hexa_exec_stream_impl with a freshly-constructed TAG_FN HexaVal.
// After a dispatch self-rebuild activates the codegen_c2 lowering with proper
// gen2_expr wrapping, this macro becomes a no-op (the second arg will
// already be a HexaVal — _Generic could choose impl directly, but the
// double-wrap path remains safe for HexaVal too via a passthrough).
//
// We keep both names: hexa_exec_stream (callable from hexa) and
// hexa_exec_stream_impl (the actual C function). The macro picks based on
// whether the second argument is a function pointer or a HexaVal.
extern HexaVal hexa_exec_stream_impl(HexaVal cmd, HexaVal on_line);
extern HexaVal hexa_fn_new(void* fp, int arity);
static inline HexaVal __hexa_exec_stream_wrap_fp(HexaVal cmd, HexaVal (*fp)(HexaVal)) {
    return hexa_exec_stream_impl(cmd, hexa_fn_new((void*)fp, 1));
}
static inline HexaVal __hexa_exec_stream_wrap_hv(HexaVal cmd, HexaVal cb) {
    return hexa_exec_stream_impl(cmd, cb);
}
// _Generic dispatch — selects the function-pointer wrapper for raw symbols
// or the HexaVal passthrough when the lowering path supplies a value.
#define hexa_exec_stream(cmd, cb) _Generic((cb), \
    HexaVal: __hexa_exec_stream_wrap_hv, \
    default: __hexa_exec_stream_wrap_fp)((cmd), (cb))

// hexa_exec_with_status: like hexa_exec but returns [stdout_str, exit_code].
// 2026-04-17: needed because the interpreter's exec_with_status dispatch in
// hexa_full.hexa silently always returns exit=0 (popen output without
// pclose status), letting build scripts roll forward stale binaries through
// failed clang/cargo invocations. Codegen + dispatch wiring TBD — this
// function is the C-runtime backstop ready when the interpreter learns to
// call it via codegen lowering of `exec_with_status(cmd)`.
HexaVal hexa_exec_with_status(HexaVal cmd) {
    HexaVal arr = hexa_array_new();
    if (!HX_IS_STR(cmd)) {
        arr = hexa_array_push(arr, hexa_str(""));
        arr = hexa_array_push(arr, hexa_int(127));
        return arr;
    }
    FILE* fp = popen(HX_STR(cmd), "r");
    if (!fp) {
        arr = hexa_array_push(arr, hexa_str(""));
        arr = hexa_array_push(arr, hexa_int(127));
        return arr;
    }
    // 2026-04-26 cross-repo upstream: same fgets→fread switch as hexa_exec
    // above (see that function's comment for the embedded-NUL truncation
    // root cause and the residual string-ABI caveat).
    char buf[4096]; size_t total = 0;
    size_t cap = 4096;
    char* result = (char*)malloc(cap); result[0] = '\0';
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        while (total + n + 1 > cap) { cap *= 2; result = (char*)realloc(result, cap); }
        memcpy(result + total, buf, n);
        total += n;
    }
    result[total] = '\0';
    int rc = pclose(fp);
    int exit_code;
    if (WIFEXITED(rc))      exit_code = WEXITSTATUS(rc);
    else if (WIFSIGNALED(rc)) exit_code = 128 + WTERMSIG(rc);
    else                     exit_code = -1;
    arr = hexa_array_push(arr, hexa_str_own(result));
    arr = hexa_array_push(arr, hexa_int(exit_code));
    return arr;
}

// exec_replace — replace current process via execvp("/bin/sh", "-c", cmd).
// Does not return on success. Used by `hexa lsp` to hand stdin/stdout fully
// to the interp interpreter so bidirectional streaming (JSON-RPC) works
// without popen buffering or the __HEXA_RC sentinel interleaving into the
// response body. On failure, returns empty string and the caller falls
// back to regular hexa_exec().
HexaVal hexa_exec_replace(HexaVal cmd) {
    if (!HX_IS_STR(cmd)) return hexa_str("");
    // fflush to avoid duplicated stdio buffers in the replacement.
    fflush(stdout); fflush(stderr);
    char* argv[4];
    argv[0] = (char*)"/bin/sh";
    argv[1] = (char*)"-c";
    argv[2] = HX_STR(cmd);
    argv[3] = NULL;
    execvp(argv[0], argv);
    // execvp returned → failure; surface errno and fall through.
    perror("exec_replace");
    return hexa_str("");
}

// ── Stderr ──────────────────────────────────────────
void hexa_eprint_val(HexaVal v) {
    // 2026-04-20 silent-fallback fix: prior body dropped TAG_VOID /
    // TAG_ARRAY / TAG_MAP / TAG_VALSTRUCT entirely (no output, not even
    // "<value>"). User-visible path is eprintln(arr) / eprintln(map) —
    // emitting nothing is strictly worse than the hexa_to_string repr.
    // One-hop TAG_VALSTRUCT unwrap mirrors hexa_print_val.
    if (HX_IS_VALSTRUCT(v) && HX_VS(v)) {
        HexaValStruct* vs = HX_VS(v);
        switch (vs->tag_i) {
            case TAG_INT:   fprintf(stderr, "%lld", (long long)vs->int_val); return;
            case TAG_FLOAT: fprintf(stderr, "%g", vs->float_val); return;
            case TAG_BOOL:  fprintf(stderr, "%s", vs->bool_val ? "true" : "false"); return;
            case TAG_STR: {
                const char* cs = HX_STR(vs->str_val);
                if (cs) { fprintf(stderr, "%s", cs); return; }
                break;
            }
            case TAG_VOID:  fprintf(stderr, "void"); return;
        }
        // fall through for compound inner tags
    }
    if (HX_IS_STR(v)) {
        if (HX_STR(v)) fprintf(stderr, "%s", HX_STR(v));
        else fprintf(stderr, "<str null>");
    }
    else if (HX_IS_INT(v)) fprintf(stderr, "%lld", (long long)HX_INT(v));
    else if (HX_IS_FLOAT(v)) fprintf(stderr, "%g", HX_FLOAT(v));
    else if (HX_IS_BOOL(v)) fprintf(stderr, HX_BOOL(v) ? "true" : "false");
    else if (HX_IS_VOID(v)) fprintf(stderr, "void");
    else if (HX_IS_ARRAY(v) || HX_IS_MAP(v)) {
        // Delegate to hexa_to_string for container repr (depth/element
        // caps are inherited). Guards against null arr/map pointers.
        HexaVal s = hexa_to_string(v);
        if (HX_IS_STR(s) && HX_STR(s)) fprintf(stderr, "%s", HX_STR(s));
        else fprintf(stderr, "<value>");
    }
    else fprintf(stderr, "<value>");
}

// ── Try/catch (setjmp based) ────────────────────────
#include <setjmp.h>
#define HEXA_MAX_TRY 64
static jmp_buf* __hexa_try_stack[HEXA_MAX_TRY];
static int __hexa_try_top = 0;
static HexaVal __hexa_error_val;

void __hexa_try_push(jmp_buf* jb) { if (__hexa_try_top < HEXA_MAX_TRY) __hexa_try_stack[__hexa_try_top++] = jb; }
void __hexa_try_pop(void) { if (__hexa_try_top > 0) __hexa_try_top--; }
HexaVal __hexa_last_error(void) { return __hexa_error_val; }

// __hexa_try_cleanup: GCC/Clang `cleanup` attribute helper used by codegen.
// Restores try-stack depth on scope exit, so an early return / break / longjmp
// inside a try block can't leak entries onto __hexa_try_stack.
void __hexa_try_cleanup(int* saved) { if (__hexa_try_top > *saved) __hexa_try_top = *saved; }

void hexa_throw(HexaVal err) {
    __hexa_error_val = err;
    if (__hexa_try_top > 0) {
        __hexa_try_top--;
        longjmp(*__hexa_try_stack[__hexa_try_top], 1);
    }
    // hxa-20260423-013: print the error value on uncaught throw so tests
    // (T34) and users can see what failed. Falls back to generic label
    // when the value can't be rendered as a short scalar.
    if (HX_IS_STR(err) && HX_STR(err)) {
        fprintf(stderr, "%s\n", HX_STR(err));
    } else {
        fprintf(stderr, "Unhandled throw\n");
    }
    exit(1);
}

// ── Printing ─────────────────────────────────────────────

// anima P0 bug #6 (2026-04-18): TAG_VALSTRUCT unwrap guard.
// When interpreter routes print through hexa_println with an arg that
// got boxed as TAG_VALSTRUCT (rt 32-G flat struct), the prior `default:`
// branch rendered "<value>" — correct-ish but lossy for the common
// string path (if true { println("inside") } inside a fn body).
// Unwrap the inner tag here so the user-visible output matches the
// intent even when Val-boxing sneaks through the interp boundary.
// See: handoff 2026-04-18 bug #6, feedback_val_corruption_class_2026_04_17.md
void hexa_print_val(HexaVal v) {
    // One-hop TAG_VALSTRUCT unwrap. Not recursive — a VS wrapping a VS is
    // already a corruption signal we surface via the fallback tail.
    if (HX_IS_VALSTRUCT(v) && HX_VS(v)) {
        HexaValStruct* vs = HX_VS(v);
        switch (vs->tag_i) {
            case TAG_INT:   printf("%lld", (long long)vs->int_val); return;
            case TAG_FLOAT: printf("%g", vs->float_val); return;
            case TAG_BOOL:  printf("%s", vs->bool_val ? "true" : "false"); return;
            case TAG_STR: {
                const char* cs = HX_STR(vs->str_val);
                if (cs) { printf("%s", cs); return; }
                break;
            }
            case TAG_VOID:  printf("void"); return;
        }
        // Inner tag not one of the printable scalars — fall through to the
        // outer switch with the boxed tag so we emit "<value>" rather than
        // dereferencing a compound descriptor that may not survive arena
        // rewinding (T33 class).
    }
    switch (HX_TAG(v)) {
        case TAG_INT: printf("%lld", (long long)HX_INT(v)); break;
        case TAG_FLOAT: printf("%g", HX_FLOAT(v)); break;
        case TAG_BOOL: printf("%s", HX_BOOL(v) ? "true" : "false"); break;
        case TAG_STR:
            // Null-pointer guard: a corrupted TAG_STR (str_val=NULL) would
            // crash printf("%s", NULL) on some libc builds. Surface it.
            if (HX_STR(v)) printf("%s", HX_STR(v));
            else printf("<str null>");
            break;
        case TAG_VOID: printf("void"); break;
        case TAG_ARRAY:
            printf("[");
            for (int i = 0; i < HX_ARR_LEN(v); i++) {
                if (i > 0) printf(", ");
                hexa_print_val(HX_ARR_ITEMS(v)[i]);
            }
            printf("]");
            break;
        case TAG_MAP: {
            // 2026-04-20 silent-fallback fix: TAG_MAP branch added.
            // Prior default emitted "<value>" — lossy for println(map).
            // Delegate to hexa_to_string so depth/element caps apply.
            HexaVal s = hexa_to_string(v);
            if (HX_IS_STR(s) && HX_STR(s)) printf("%s", HX_STR(s));
            else printf("<value>");
            break;
        }
        default: printf("<value>"); break;
    }
}

void hexa_println(HexaVal v) { hexa_print_val(v); printf("\n"); }

// P7-6 builtin (2026-04-18): stderr counterpart to hexa_println. Used
// by user code that needs to emit diagnostics without polluting stdout
// (e.g. CLI tools whose stdout is a result stream piped to another
// process). Returns void-equivalent HexaVal so it can slot into the
// uniform i64-ret IR shape that the native_build dispatcher emits.
HexaVal hexa_eprintln(HexaVal v) {
    // Route through the same hexa_print_val logic but redirect the
    // writes to stderr. We cannot reuse hexa_print_val directly because
    // it hard-codes printf() — mirror the body with fprintf(stderr).
    if (HX_IS_VALSTRUCT(v) && HX_VS(v)) {
        HexaValStruct* vs = HX_VS(v);
        switch (vs->tag_i) {
            case TAG_INT:   fprintf(stderr, "%lld\n", (long long)vs->int_val); return hexa_void();
            case TAG_FLOAT: fprintf(stderr, "%g\n", vs->float_val); return hexa_void();
            case TAG_BOOL:  fprintf(stderr, "%s\n", vs->bool_val ? "true" : "false"); return hexa_void();
            case TAG_STR: {
                const char* cs = HX_STR(vs->str_val);
                if (cs) { fprintf(stderr, "%s\n", cs); return hexa_void(); }
                break;
            }
            case TAG_VOID:  fprintf(stderr, "void\n"); return hexa_void();
        }
        // fall through to outer switch for corrupted/compound inner tags
    }
    switch (HX_TAG(v)) {
        case TAG_INT: fprintf(stderr, "%lld\n", (long long)HX_INT(v)); break;
        case TAG_FLOAT: fprintf(stderr, "%g\n", HX_FLOAT(v)); break;
        case TAG_BOOL: fprintf(stderr, "%s\n", HX_BOOL(v) ? "true" : "false"); break;
        case TAG_STR:
            if (HX_STR(v)) fprintf(stderr, "%s\n", HX_STR(v));
            else fprintf(stderr, "<str null>\n");
            break;
        case TAG_VOID: fprintf(stderr, "void\n"); break;
        case TAG_ARRAY:
        case TAG_MAP: {
            // 2026-04-20 silent-fallback fix: TAG_ARRAY/TAG_MAP branches
            // added. Prior default emitted "<value>\n" — lossy for
            // eprintln(arr) / eprintln(map). Delegate to hexa_to_string.
            HexaVal s = hexa_to_string(v);
            if (HX_IS_STR(s) && HX_STR(s)) fprintf(stderr, "%s\n", HX_STR(s));
            else fprintf(stderr, "<value>\n");
            break;
        }
        default: fprintf(stderr, "<value>\n"); break;
    }
    return hexa_void();
}

// 2026-04-20 builtin: stderr + NO trailing newline (print/println symmetry).
// Mirrors hexa_eprintln byte-for-byte except the "\n" suffix is dropped from
// every format branch. Motivating callers:
//   tool/pkg/packages/token-forge/proxy/server.hexa
//   self/ml/corpus_clean.hexa
// which pre-2026-04-20 fell through to hexa_call1(eprint,...) and produced a
// clang link error in AOT. Interp path also route-checks this name.
HexaVal hexa_eprint(HexaVal v) {
    if (HX_IS_VALSTRUCT(v) && HX_VS(v)) {
        HexaValStruct* vs = HX_VS(v);
        switch (vs->tag_i) {
            case TAG_INT:   fprintf(stderr, "%lld", (long long)vs->int_val); return hexa_void();
            case TAG_FLOAT: fprintf(stderr, "%g", vs->float_val); return hexa_void();
            case TAG_BOOL:  fprintf(stderr, "%s", vs->bool_val ? "true" : "false"); return hexa_void();
            case TAG_STR: {
                const char* cs = HX_STR(vs->str_val);
                if (cs) { fprintf(stderr, "%s", cs); return hexa_void(); }
                break;
            }
            case TAG_VOID:  fprintf(stderr, "void"); return hexa_void();
        }
    }
    switch (HX_TAG(v)) {
        case TAG_INT: fprintf(stderr, "%lld", (long long)HX_INT(v)); break;
        case TAG_FLOAT: fprintf(stderr, "%g", HX_FLOAT(v)); break;
        case TAG_BOOL: fprintf(stderr, "%s", HX_BOOL(v) ? "true" : "false"); break;
        case TAG_STR:
            if (HX_STR(v)) fprintf(stderr, "%s", HX_STR(v));
            else fprintf(stderr, "<str null>");
            break;
        case TAG_VOID: fprintf(stderr, "void"); break;
        case TAG_ARRAY:
        case TAG_MAP: {
            HexaVal s = hexa_to_string(v);
            if (HX_IS_STR(s) && HX_STR(s)) fprintf(stderr, "%s", HX_STR(s));
            else fprintf(stderr, "<value>");
            break;
        }
        default: fprintf(stderr, "<value>"); break;
    }
    return hexa_void();
}

// ── to_string ────────────────────────────────────────────

// 2026-04-20 container-repr fix: TAG_ARRAY/TAG_MAP branches added so
// `str(arr)` / `str(map)` produce readable `[e1, e2]` / `{k: v, ...}`
// output instead of the "<value>" fallback. Mirrors hexa_print_val
// (runtime.c:2796) which already handled TAG_ARRAY — the asymmetry
// between hexa_print_val (direct print) and hexa_to_string (string
// concat via `+`) was surfacing as `"arr=" + str(arr) -> "arr=<value>"`.
// Guards: recursion depth cap (8) to prevent stack blow-up on cyclic
// references; element cap (64) so a 1M-element tensor-as-array does
// not allocate a multi-MB string; null pointer guards on arr_ptr /
// map_ptr / order_keys before deref (T33 arena-dangling defence).
static HexaVal _hexa_to_string_rec(HexaVal v, int depth);

HexaVal hexa_to_string(HexaVal v) {
    return _hexa_to_string_rec(v, 0);
}

static HexaVal _hexa_to_string_rec(HexaVal v, int depth) {
    if (!_cached_strs_ready) _hexa_init_cached_strs();
    char buf[64];
    switch (HX_TAG(v)) {
        case TAG_INT: {
            // ROI-33: small-int cache hit — skip snprintf+intern entirely
            if (HX_INT(v) >= SMALL_INT_CACHE_MIN && HX_INT(v) <= SMALL_INT_CACHE_MAX) {
                if (!_cached_small_ints_ready) _hexa_init_small_int_cache();
                return _cached_small_ints[HX_INT(v) - SMALL_INT_CACHE_MIN];
            }
            snprintf(buf, 64, "%lld", (long long)HX_INT(v));
            return hexa_str(buf);
        }
        case TAG_FLOAT: snprintf(buf, 64, "%g", HX_FLOAT(v)); return hexa_str(buf);
        case TAG_BOOL: return HX_BOOL(v) ? _cached_str_true : _cached_str_false;
        case TAG_STR: return v;
        case TAG_VOID: return _cached_str_void;
        // rt 32-G: minimal fallback repr — interpreter has its own
        // val_to_string for the full form.
        case TAG_VALSTRUCT: {
            if (!HX_VS(v)) return hexa_str("<Val null>");
            snprintf(buf, 64, "Val{tag=%lld,i=%lld}",
                (long long)HX_VSF(v, tag_i), (long long)HX_VSF(v, int_val));
            return hexa_str(buf);
        }
        case TAG_ARRAY: {
            if (depth >= 8) return hexa_str("[...]");
            if (!v.arr_ptr) return hexa_str("[<null>]");
            int n = HX_ARR_LEN(v);
            int cap = 64;  // element-dump cap
            HexaVal out = hexa_str("[");
            HexaVal sep = hexa_str(", ");
            int shown = n < cap ? n : cap;
            for (int i = 0; i < shown; i++) {
                if (i > 0) out = hexa_str_concat(out, sep);
                out = hexa_str_concat(out, _hexa_to_string_rec(HX_ARR_ITEMS(v)[i], depth + 1));
            }
            if (n > cap) {
                snprintf(buf, 64, ", ... (%d more)", n - cap);
                out = hexa_str_concat(out, hexa_str(buf));
            }
            out = hexa_str_concat(out, hexa_str("]"));
            return out;
        }
        case TAG_MAP: {
            if (depth >= 8) return hexa_str("{...}");
            HexaMapTable* t = HX_MAP_TBL(v);
            if (!t) return hexa_str("{}");
            int n = t->len;
            int cap = 64;
            HexaVal out = hexa_str("{");
            HexaVal sep = hexa_str(", ");
            HexaVal kv_sep = hexa_str(": ");
            int shown = n < cap ? n : cap;
            for (int i = 0; i < shown; i++) {
                if (i > 0) out = hexa_str_concat(out, sep);
                const char* k = (t->order_keys && t->order_keys[i]) ? t->order_keys[i] : "<null>";
                out = hexa_str_concat(out, hexa_str(k));
                out = hexa_str_concat(out, kv_sep);
                out = hexa_str_concat(out, _hexa_to_string_rec(t->order_vals[i], depth + 1));
            }
            if (n > cap) {
                snprintf(buf, 64, ", ... (%d more)", n - cap);
                out = hexa_str_concat(out, hexa_str(buf));
            }
            out = hexa_str_concat(out, hexa_str("}"));
            return out;
        }
        default: return _cached_str_value;
    }
}

// ── Truthy check ─────────────────────────────────────────

int hexa_truthy(HexaVal v) {
    switch (HX_TAG(v)) {
        case TAG_BOOL: return HX_BOOL(v);
        case TAG_INT: return HX_INT(v) != 0;
        // 2026-04-20 silent-fallback audit: TAG_FLOAT was falling through
        // to `default: return 1`, so `if 0.0 { ... }` evaluated truthy.
        // Match TAG_INT semantics — zero is falsy. NaN is truthy (non-zero).
        case TAG_FLOAT: return HX_FLOAT(v) != 0.0;
        case TAG_STR: return HX_STR(v) != NULL && HX_STR(v)[0] != 0;
        case TAG_VOID: return 0;
        // rt 32-G: TAG_VALSTRUCT truthy iff .vs pointer non-null.
        case TAG_VALSTRUCT: return HX_VS(v) != NULL;
        default: return 1;
    }
}

// ── Type checking ────────────────────────────────────────

HexaVal hexa_type_of(HexaVal v) {
    if (!_cached_strs_ready) _hexa_init_cached_strs();
    switch (HX_TAG(v)) {
        case TAG_INT: return _cached_str_int;
        case TAG_FLOAT: return _cached_str_float;
        case TAG_BOOL: return _cached_str_bool;
        case TAG_STR: return _cached_str_string;
        case TAG_VOID: return _cached_str_void;
        case TAG_ARRAY: return _cached_str_array;
        case TAG_MAP: return _cached_str_map;
        // 2026-04-20 silent-fallback audit: prior default dropped TAG_FN /
        // TAG_CHAR / TAG_CLOSURE into "unknown" — Hexa-side dispatch like
        // `type_of(x) != "string"` then misclassified callable values.
        // Surface them explicitly; any remaining unknown tag is a real bug.
        case TAG_FN: return _cached_str_fn;
        case TAG_CHAR: return _cached_str_char;
        case TAG_CLOSURE: return _cached_str_closure;
        // rt 32-G: Val is a struct at the Hexa level; surface "struct".
        case TAG_VALSTRUCT: return _cached_str_struct;
        default: return _cached_str_unknown;
    }
}

// ── Polymorphic add (int + or string concat) ─────────────

// ROI-37: rename impl to _slow so the macro below can inline the int+int
// fast path without function-call ABI overhead.
static HexaVal hexa_add_slow(HexaVal a, HexaVal b) {
    if (HX_IS_INT(a) && HX_IS_INT(b)) return hexa_int(HX_INT(a) + HX_INT(b));
    if (HX_IS_FLOAT(a) || HX_IS_FLOAT(b)) {
        // T39: unwrap via __hx_to_double so TAG_VALSTRUCT wrappers don't
        // cast the vs pointer to double (same root as T32 to_float bug).
        double fa = __hx_to_double(a);
        double fb = __hx_to_double(b);
        return hexa_float(fa + fb);
    }
    // B13: array concat. Interpreter (hexa_full.hexa:6954) does this but
    // codegen emits bare hexa_add(l,r) — without this branch, two arrays
    // fall through to to_string+str_concat → TAG_STR, then later arr[i]
    // reads .arr.len from a TAG_STR union slot (stack residue → random OOB).
    if (HX_IS_ARRAY(a) && HX_IS_ARRAY(b)) {
        HexaVal out = hexa_array_new();
        for (int i = 0; i < HX_ARR_LEN(a); i++) out = hexa_array_push(out, HX_ARR_ITEMS(a)[i]);
        for (int i = 0; i < HX_ARR_LEN(b); i++) out = hexa_array_push(out, HX_ARR_ITEMS(b)[i]);
        return out;
    }
    HexaVal sa = hexa_to_string(a);
    HexaVal sb = hexa_to_string(b);
    return hexa_str_concat(sa, sb);
}

// ROI-37: inline macro — int+int hot path avoids 32-byte HexaVal call ABI.
// Evaluates each operand exactly once via temporaries.
#define hexa_add(A, B) \
    ({ HexaVal __ha = (A), __hb = (B); \
       (HX_IS_INT(__ha) && HX_IS_INT(__hb)) \
           ? hexa_int(HX_INT(__ha) + HX_INT(__hb)) \
           : hexa_add_slow(__ha, __hb); })

// ── Variadic concat (N-way) — clang bracket-depth relief ─────
// codegen_c2 flattens long `+` chains (≥16 operands) into a single call:
//   hexa_concat_many(N, (HexaVal[]){e1, e2, ..., eN})
// instead of hexa_add(hexa_add(hexa_add(...))) which blows past clang's
// default 256-level bracket nesting limit on anima launcher files
// (launch_alm_14b_r9.hexa ~170 deep → fatal). Semantics = left-fold via
// hexa_add so mixed int/string/array inputs follow the same rules as the
// pairwise operator.
HexaVal hexa_concat_many(int n, HexaVal* parts) {
    if (n <= 0) return hexa_str("");
    HexaVal acc = parts[0];
    for (int i = 1; i < n; i++) {
        acc = hexa_add(acc, parts[i]);
    }
    return acc;
}

// ── Polymorphic equality ─────────────────────────────────

HexaVal hexa_eq(HexaVal a, HexaVal b) {
    // Cross-type numeric equality (int ↔ float) — matches interp
    // eval_binop float-coerce path (hexa_full.hexa:7867). 이전엔
    // tag 불일치 즉시 false → `2 == 2.0` AOT=false / interp=true
    // divergence 를 유발.
    if (HX_IS_INT(a) && HX_IS_FLOAT(b)) return hexa_bool((double)HX_INT(a) == HX_FLOAT(b));
    if (HX_IS_FLOAT(a) && HX_IS_INT(b)) return hexa_bool(HX_FLOAT(a) == (double)HX_INT(b));
    if (HX_TAG(a) != HX_TAG(b)) return hexa_bool(0);
    switch (HX_TAG(a)) {
        case TAG_INT: return hexa_bool(HX_INT(a) == HX_INT(b));
        case TAG_FLOAT: return hexa_bool(HX_FLOAT(a) == HX_FLOAT(b));
        case TAG_BOOL: return hexa_bool(HX_BOOL(a) == HX_BOOL(b));
        case TAG_STR: return hexa_bool(HX_STR(a) == HX_STR(b) || strcmp(HX_STR(a), HX_STR(b)) == 0);
        case TAG_VOID: return hexa_bool(1);
        // rt 32-G: Val identity is pointer-equality of heap struct (matches
        // TAG_MAP semantics — two separately constructed maps never compare
        // equal by value).
        case TAG_VALSTRUCT: return hexa_bool(HX_VS(a) == HX_VS(b));
        // Array deep equality — interp eval_binop (hexa_full.hexa:7939) 가
        // 요소별 vals_equal 로 비교하므로 AOT 도 요소 재귀 비교로 정렬.
        // 이전 comment 는 interp 가 ref eq 이라고 기록했지만 실제 코드는
        // value eq. 같은 포인터 shortcut 으로 빠른 경로 유지.
        case TAG_ARRAY: {
            if (a.arr_ptr == b.arr_ptr) return hexa_bool(1);
            int __na = HX_ARR_LEN(a); int __nb = HX_ARR_LEN(b);
            if (__na != __nb) return hexa_bool(0);
            HexaVal* __xa = HX_ARR_ITEMS(a); HexaVal* __xb = HX_ARR_ITEMS(b);
            for (int __i = 0; __i < __na; __i++) {
                if (!hexa_truthy(hexa_eq(__xa[__i], __xb[__i]))) return hexa_bool(0);
            }
            return hexa_bool(1);
        }
        case TAG_MAP: return hexa_bool(HX_MAP_TBL(a) == HX_MAP_TBL(b));
        // TAG_FN: compare underlying C function pointer (descriptor indirection
        // may differ for two separately-constructed wrappers of the same fn).
        case TAG_FN:
            return hexa_bool(
                (a.fn_ptr_d && b.fn_ptr_d)
                    ? (HX_FN_PTR(a) == HX_FN_PTR(b))
                    : (a.fn_ptr_d == b.fn_ptr_d));
        case TAG_CHAR: return hexa_bool(HX_INT(a) == HX_INT(b));
        case TAG_CLOSURE:
            return hexa_bool(
                (a.clo_ptr && b.clo_ptr)
                    ? (HX_CLO_PTR(a) == HX_CLO_PTR(b))
                    : (a.clo_ptr == b.clo_ptr));
        default: return hexa_bool(0);
    }
}

// ── File I/O ─────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────
// M1 full · file layer (hxa-20260423-003 Step 3)
// rt_file_* hand-port — authoritative implementations for file I/O.
// hexa_* wrappers below are shims forwarding to rt_* (preserves external
// ABI: self/bootstrap_compiler.c, build_c.hexa emit, codegen emit).
// ─────────────────────────────────────────────────────────────────────

HexaVal rt_read_file(HexaVal path) {
    FILE* f = fopen(HX_STR(path), "rb");
    if (!f) return hexa_str("");
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = 0;
    fclose(f);
    return hexa_str_own(buf);
}

HexaVal rt_write_file(HexaVal path, HexaVal content) {
    FILE* f = fopen(HX_STR(path), "wb");
    if (!f) return hexa_bool(0);
    const char* s = HX_STR(content);
    size_t len = s ? strlen(s) : 0;
    fwrite(s, 1, len, f);
    fclose(f);
    return hexa_bool(1);
}

// P7-6 builtin (2026-04-18): boolean existence probe. Used by native_build
// and other self-hosting scripts to gate optional inputs without a
// full read_file round-trip.
//
// ω-stdlib-2 (2026-04-26): refined to S_ISREG semantics, matching the
// portable_fs.hexa POSIX-wc contract that consumers were forced to alias
// around (pfs_file_exists). Previously used access(p, F_OK) which returns
// true for ANY path entry incl. directories / symlinks-to-dirs / sockets.
// Now: stat() + S_ISREG → true only for regular files. Null/missing/non-
// regular → false. No errno propagation.
HexaVal rt_file_exists(HexaVal path) {
    const char* p = HX_STR(path);
    if (!p) return hexa_bool(0);
    struct stat st;
    if (stat(p, &st) != 0) return hexa_bool(0);
    return hexa_bool(S_ISREG(st.st_mode) ? 1 : 0);
}

// ── Binary file I/O — write_bytes / read_file_bytes ─────
// write_bytes: takes a path and a TAG_ARRAY of integers (0-255),
// writes raw bytes. Null bytes survive — true binary I/O.
HexaVal rt_write_bytes(HexaVal path, HexaVal arr) {
    if (!HX_IS_ARRAY(arr)) return hexa_bool(0);
    int len = HX_ARR_LEN(arr);
    FILE* f = fopen(HX_STR(path), "wb");
    if (!f) return hexa_bool(0);
    if (len > 0) {
        unsigned char* buf = (unsigned char*)malloc(len);
        HexaVal* items = HX_ARR_ITEMS(arr);
        for (int i = 0; i < len; i++) {
            int64_t v = HX_IS_INT(items[i]) ? HX_INT(items[i]) : (int64_t)HX_FLOAT(items[i]);
            buf[i] = (unsigned char)(v & 0xFF);
        }
        fwrite(buf, 1, len, f);
        free(buf);
    }
    fclose(f);
    return hexa_bool(1);
}

// rt_write_bytes_v: variant called from the interpreter dispatch where each
// array item is a TAG_VALSTRUCT (Val) rather than a raw HexaVal int. Reads
// the inner `int_val` field from each Val and writes the low byte. Without
// this, write_bytes from interpreted user code silently no-ops (HX_IS_INT
// fails on TAG_VALSTRUCT items) or — with the historical fputs path — UTF-8
// encodes each byte > 0x7F to two bytes. (2026-04-17)
HexaVal rt_write_bytes_v(HexaVal path, HexaVal arr) {
    if (!HX_IS_ARRAY(arr)) return hexa_bool(0);
    int len = HX_ARR_LEN(arr);
    FILE* f = fopen(HX_STR(path), "wb");
    if (!f) return hexa_bool(0);
    if (len > 0) {
        unsigned char* buf = (unsigned char*)malloc(len);
        HexaVal* items = HX_ARR_ITEMS(arr);
        for (int i = 0; i < len; i++) {
            HexaVal item = items[i];
            int64_t v = 0;
            if (HX_IS_INT(item)) {
                v = HX_INT(item);
            } else if (HX_TAG(item) == TAG_FLOAT) {
                v = (int64_t)HX_FLOAT(item);
            } else if (HX_TAG(item) == TAG_VALSTRUCT) {
                HexaVal iv = hexa_valstruct_int(item);
                if (HX_IS_INT(iv)) {
                    v = HX_INT(iv);
                } else if (HX_TAG(iv) == TAG_FLOAT) {
                    v = (int64_t)HX_FLOAT(iv);
                }
            }
            buf[i] = (unsigned char)(v & 0xFF);
        }
        fwrite(buf, 1, len, f);
        free(buf);
    }
    fclose(f);
    return hexa_bool(1);
}

// read_file_bytes: reads a file in binary mode and returns
// a TAG_ARRAY of integers (0-255), one per byte.
HexaVal rt_read_file_bytes(HexaVal path) {
    FILE* f = fopen(HX_STR(path), "rb");
    if (!f) return hexa_array_new();
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* buf = (unsigned char*)malloc(len);
    fread(buf, 1, len, f);
    fclose(f);
    // Pre-allocate array with exact capacity
    HexaVal arr = hexa_array_new();
    if (len > 0) {
        HX_SET_ARR_ITEMS(arr, (HexaVal*)malloc(sizeof(HexaVal) * len));
        HX_SET_ARR_CAP(arr, (int)len);
        HX_SET_ARR_LEN(arr, (int)len);
        HexaVal* items = HX_ARR_ITEMS(arr);
        for (long i = 0; i < len; i++) {
            items[i] = hexa_int((int64_t)buf[i]);
        }
    }
    free(buf);
    return arr;
}

// ── P4 streaming builtins — read_bytes_at / write_bytes_append ─────
// Required for multi-GB safetensors→hexaw conversion on 14B Qwen2.5
// (8 shards × ~3.7GB each). Whole-file read_file_bytes cannot fit.

// read_bytes_at(path, offset, nbytes): pread-style ranged read.
// Returns a TAG_ARRAY of at most `nbytes` ints (shorter if EOF reached
// or offset is past EOF). Empty array on fopen failure.
HexaVal rt_read_bytes_at(HexaVal path, HexaVal offset, HexaVal nbytes) {
    int64_t off = HX_IS_INT(offset) ? HX_INT(offset)
                : (HX_TAG(offset) == TAG_FLOAT ? (int64_t)HX_FLOAT(offset) : 0);
    int64_t req = HX_IS_INT(nbytes) ? HX_INT(nbytes)
                : (HX_TAG(nbytes) == TAG_FLOAT ? (int64_t)HX_FLOAT(nbytes) : 0);
    if (off < 0 || req <= 0) return hexa_array_new();
    FILE* f = fopen(HX_STR(path), "rb");
    if (!f) return hexa_array_new();
    // Use 64-bit seek where available; on macOS/Linux fseeko is standard.
#if defined(_WIN32)
    if (_fseeki64(f, (long long)off, SEEK_SET) != 0) { fclose(f); return hexa_array_new(); }
#else
    if (fseeko(f, (off_t)off, SEEK_SET) != 0) { fclose(f); return hexa_array_new(); }
#endif
    unsigned char* buf = (unsigned char*)malloc((size_t)req);
    size_t got = fread(buf, 1, (size_t)req, f);
    fclose(f);
    HexaVal arr = hexa_array_new();
    if (got > 0) {
        HX_SET_ARR_ITEMS(arr, (HexaVal*)malloc(sizeof(HexaVal) * got));
        HX_SET_ARR_CAP(arr, (int)got);
        HX_SET_ARR_LEN(arr, (int)got);
        HexaVal* items = HX_ARR_ITEMS(arr);
        for (size_t i = 0; i < got; i++) {
            items[i] = hexa_int((int64_t)buf[i]);
        }
    }
    free(buf);
    return arr;
}

// write_bytes_append(path, arr): append raw bytes from an int array
// to `path` (fopen "ab"), creating the file if needed. Returns bool.
// Mirrors rt_write_bytes (raw HexaVal int array path).
HexaVal rt_write_bytes_append(HexaVal path, HexaVal arr) {
    if (!HX_IS_ARRAY(arr)) return hexa_bool(0);
    int len = HX_ARR_LEN(arr);
    FILE* f = fopen(HX_STR(path), "ab");
    if (!f) return hexa_bool(0);
    if (len > 0) {
        unsigned char* buf = (unsigned char*)malloc(len);
        HexaVal* items = HX_ARR_ITEMS(arr);
        for (int i = 0; i < len; i++) {
            int64_t v = HX_IS_INT(items[i]) ? HX_INT(items[i]) : (int64_t)HX_FLOAT(items[i]);
            buf[i] = (unsigned char)(v & 0xFF);
        }
        fwrite(buf, 1, len, f);
        free(buf);
    }
    fclose(f);
    return hexa_bool(1);
}

// write_bytes_append_v(path, arr): variant for the interpreter dispatch
// where each array item is a TAG_VALSTRUCT (Val). Mirrors write_bytes_v.
HexaVal rt_write_bytes_append_v(HexaVal path, HexaVal arr) {
    if (!HX_IS_ARRAY(arr)) return hexa_bool(0);
    int len = HX_ARR_LEN(arr);
    FILE* f = fopen(HX_STR(path), "ab");
    if (!f) return hexa_bool(0);
    if (len > 0) {
        unsigned char* buf = (unsigned char*)malloc(len);
        HexaVal* items = HX_ARR_ITEMS(arr);
        for (int i = 0; i < len; i++) {
            HexaVal item = items[i];
            int64_t v = 0;
            if (HX_IS_INT(item)) {
                v = HX_INT(item);
            } else if (HX_TAG(item) == TAG_FLOAT) {
                v = (int64_t)HX_FLOAT(item);
            } else if (HX_TAG(item) == TAG_VALSTRUCT) {
                HexaVal iv = hexa_valstruct_int(item);
                if (HX_IS_INT(iv)) {
                    v = HX_INT(iv);
                } else if (HX_TAG(iv) == TAG_FLOAT) {
                    v = (int64_t)HX_FLOAT(iv);
                }
            }
            buf[i] = (unsigned char)(v & 0xFF);
        }
        fwrite(buf, 1, len, f);
        free(buf);
    }
    fclose(f);
    return hexa_bool(1);
}

// file_size(path): native 64-bit file size without loading contents.
// Existing hexa_full dispatch read the whole file to count length — not
// viable for multi-GB safetensors shards.
//
// ω-stdlib-2 (2026-04-26): switched from fopen+fseeko/ftello to stat()
// for two reasons: (1) avoids opening the file at all — pure metadata
// query, no FD pressure, no platform-dependent fopen-on-directory
// behaviour; (2) aligns the missing-file convention with portable_fs.hexa
// (`pfs_file_size`, POSIX `wc -c`-based) which returns 0 for missing
// rather than -1. Consumers can now stop maintaining the pfs_* alias
// shadow layer. Directories / non-regular files also return 0 (size is
// only meaningful for regular files; callers needing dir-size must walk).
HexaVal rt_file_size(HexaVal path) {
    const char* p = HX_STR(path);
    if (!p) return hexa_int(0);
    struct stat st;
    if (stat(p, &st) != 0) return hexa_int(0);
    if (!S_ISREG(st.st_mode)) return hexa_int(0);
    return hexa_int((int64_t)st.st_size);
}

// ── Command line arguments ───────────────────────────

static int _hexa_argc = 0;
static char** _hexa_argv = NULL;

void hexa_set_args(int argc, char** argv) {
    // S1-D2: init TAG_FN shim globals before user code touches them.
    _hexa_init_fn_shims();
    // argv[0]을 2번 삽입하여 인터프리터 모드와 동일한 인덱스 유지
    // 인터프리터: ["hexa", "script.hexa", arg1, arg2, ...]
    // 네이티브:   ["binary", "binary",    arg1, arg2, ...]
    _hexa_argc = argc + 1;
    _hexa_argv = (char**)malloc(sizeof(char*) * (argc + 1));
    _hexa_argv[0] = argv[0];
    _hexa_argv[1] = argv[0];
    for (int i = 1; i < argc; i++) {
        _hexa_argv[i + 1] = argv[i];
    }
}

HexaVal hexa_args() {
    HexaVal arr = hexa_array_new();
    for (int i = 0; i < _hexa_argc; i++) {
        arr = hexa_array_push(arr, hexa_str(_hexa_argv[i]));
    }
    return arr;
}

// Canonical argv API (roadmap 65 / M3, anima hxa-20260423-003 §3 M3).
// Additive on top of args() — hexa_args()/hexa_set_args() untouched because
// 40+ call sites across self/ + tool/ + bench/ read args()[2..] assuming the
// current [driver, driver, user...] layout. Full contract flip requires a
// repo-wide migration (separate follow-on). These new APIs let new
// consumers (incl. anima tool/an11_a_verifier skip0 path) get clean
// semantics today: script_path() = the thing that was launched,
// real_args() = user arguments only, identical across interp/AOT/stage0.
HexaVal hexa_script_path() {
    if (_hexa_argc < 2 || _hexa_argv[1] == NULL) return hexa_str("");
    return hexa_str(_hexa_argv[1]);
}

HexaVal hexa_real_args() {
    HexaVal arr = hexa_array_new();
    int start = (_hexa_argc >= 2) ? 2 : _hexa_argc;
    for (int i = start; i < _hexa_argc; i++) {
        arr = hexa_array_push(arr, hexa_str(_hexa_argv[i]));
    }
    return arr;
}

// ── Float operations ─────────────────────────────────

#include <math.h>

// T39: math helpers route through __hx_to_double so TAG_VALSTRUCT
// wrappers (interpreter Vals) don't cast vs pointer bits to double.
HexaVal hexa_sqrt(HexaVal v) {
    return hexa_float(sqrt(__hx_to_double(v)));
}

HexaVal hexa_pow(HexaVal base, HexaVal exp) {
    // Integer fast-path: int^int with non-negative exponent stays int
    if (HX_IS_INT(base) && HX_IS_INT(exp) && HX_INT(exp) >= 0) {
        int64_t b = HX_INT(base), e = HX_INT(exp), r = 1;
        while (e > 0) { if (e & 1) r *= b; b *= b; e >>= 1; }
        return hexa_int(r);
    }
    return hexa_float(pow(__hx_to_double(base), __hx_to_double(exp)));
}

HexaVal hexa_floor(HexaVal v) {
    return hexa_int((int64_t)floor(__hx_to_double(v)));
}

/* FIX-A (Anima serving unblock, 2026-04-19): unsigned integer floor division.
 * ML kernels use `u_floor(a, b)` as the Python `a // b` surrogate for
 * non-negative operands (e.g., `n_heads // 2`). Defined on non-negative
 * int64 — for negative dividend, still truncates toward -inf like floor. */
HexaVal hexa_u_floor(HexaVal a, HexaVal b) {
    int64_t ai = HX_IS_INT(a) ? HX_INT(a) : (int64_t)__hx_to_double(a);
    int64_t bi = HX_IS_INT(b) ? HX_INT(b) : (int64_t)__hx_to_double(b);
    if (bi == 0) return hexa_int(0);
    int64_t q = ai / bi;
    int64_t r = ai % bi;
    /* floor semantics: when signs differ and remainder non-zero, round down */
    if (r != 0 && ((ai < 0) != (bi < 0))) q -= 1;
    return hexa_int(q);
}

HexaVal hexa_ceil(HexaVal v) {
    return hexa_int((int64_t)ceil(__hx_to_double(v)));
}

HexaVal hexa_abs(HexaVal v) {
    if (HX_IS_INT(v)) return hexa_int(HX_INT(v) < 0 ? -HX_INT(v) : HX_INT(v));
    return hexa_float(fabs(HX_FLOAT(v)));
}

// P7-6 math coverage (2026-04-19): transcendentals + rounding + conversions
// used by training/inference codegen. All route through __hx_to_double to
// unwrap TAG_VALSTRUCT wrappers (same pattern as hexa_sqrt).

HexaVal hexa_sin(HexaVal v)   { return hexa_float(sin(__hx_to_double(v))); }
HexaVal hexa_cos(HexaVal v)   { return hexa_float(cos(__hx_to_double(v))); }
HexaVal hexa_tan(HexaVal v)   { return hexa_float(tan(__hx_to_double(v))); }
HexaVal hexa_exp(HexaVal v)   { return hexa_float(exp(__hx_to_double(v))); }
HexaVal hexa_log(HexaVal v)   { return hexa_float(log(__hx_to_double(v))); }
HexaVal hexa_log10(HexaVal v) { return hexa_float(log10(__hx_to_double(v))); }
HexaVal hexa_round(HexaVal v) { return hexa_int((int64_t)llround(__hx_to_double(v))); }
HexaVal hexa_tanh(HexaVal v)  { return hexa_float(tanh(__hx_to_double(v))); }
HexaVal hexa_log2(HexaVal v)  { return hexa_float(log2(__hx_to_double(v))); }

// to_float: coerce any scalar to float
HexaVal hexa_to_float(HexaVal v) {
    return hexa_float(__hx_to_double(v));
}

// to_int: coerce any scalar to int (truncate toward zero for floats)
HexaVal hexa_to_int(HexaVal v) {
    if (HX_IS_INT(v)) return v;
    if (HX_IS_FLOAT(v)) return hexa_int((int64_t)HX_FLOAT(v));
    // String → int fallback via existing parser
    if (HX_IS_STR(v)) return hexa_str_parse_int(v);
    return hexa_int((int64_t)__hx_to_double(v));
}

// ── String format ────────────────────────────────────

// Multi-arg format: hexa_format_n(fmt, args_array)
HexaVal hexa_format(HexaVal fmt, HexaVal arg) {
    // Single arg: replace first {} with arg
    if (!HX_IS_STR(fmt)) return fmt;
    char* pos = strstr(HX_STR(fmt), "{}");
    if (!pos) return fmt;
    HexaVal sarg = hexa_to_string(arg);
    int before = pos - HX_STR(fmt);
    int after_len = strlen(pos + 2);
    char* result = malloc(before + strlen(HX_STR(sarg)) + after_len + 1);
    strncpy(result, HX_STR(fmt), before);
    result[before] = 0;
    strcat(result, HX_STR(sarg));
    strcat(result, pos + 2);
    return hexa_str_own(result);
}

HexaVal hexa_format_n(HexaVal fmt, HexaVal args) {
    // Multi-arg: replace {} and {:.N} with successive args
    if (!HX_IS_STR(fmt) || !HX_IS_ARRAY(args)) return fmt;
    size_t cap = strlen(HX_STR(fmt)) * 4 + HX_ARR_LEN(args) * 64 + 256;
    char* result = malloc(cap);
    size_t total = 0;
    result[0] = 0;
    char* src = HX_STR(fmt);
    int ai = 0;
    while (*src) {
        if (src[0] == '{' && ai < HX_ARR_LEN(args)) {
            // Find closing }
            char* close = strchr(src, '}');
            if (close) {
                int speclen = (int)(close - src - 1);
                char spec[32] = {0};
                if (speclen > 0 && speclen < 31) memcpy(spec, src+1, speclen);
                char buf[128];
                HexaVal arg = HX_ARR_ITEMS(args)[ai++];
                if (spec[0] == ':' && spec[1] == '.') {
                    // Precision format {:.N}
                    int prec = atoi(spec + 2);
                    double val = HX_IS_FLOAT(arg) ? HX_FLOAT(arg) : (double)HX_INT(arg);
                    snprintf(buf, sizeof(buf), "%.*f", prec, val);
                } else if (spec[0] == 0) {
                    // Simple {}
                    HexaVal s = hexa_to_string(arg);
                    strncpy(buf, HX_STR(s), sizeof(buf)-1); buf[sizeof(buf)-1]=0;
                } else {
                    HexaVal s = hexa_to_string(arg);
                    strncpy(buf, HX_STR(s), sizeof(buf)-1); buf[sizeof(buf)-1]=0;
                }
                size_t blen = strlen(buf);
                while (total + blen + 1 > cap) { cap *= 2; result = realloc(result, cap); }
                memcpy(result + total, buf, blen);
                total += blen;
                result[total] = 0;
                src = close + 1;
            } else {
                while (total + 2 > cap) { cap *= 2; result = realloc(result, cap); }
                result[total++] = *src;
                result[total] = 0;
                src++;
            }
        } else {
            while (total + 2 > cap) { cap *= 2; result = realloc(result, cap); }
            result[total++] = *src;
            result[total] = 0;
            src++;
        }
    }
    return hexa_str_own(result);
}

// ── String split ─────────────────────────────────────

HexaVal hexa_str_split(HexaVal s, HexaVal delim) {
    HexaVal arr = hexa_array_new();
    if (!HX_IS_STR(s) || !HX_IS_STR(delim)) return arr;
    char* src = strdup(HX_STR(s));
    char* d = HX_STR(delim);
    int dlen = strlen(d);
    char* pos = src;
    while (1) {
        char* found = strstr(pos, d);
        if (!found) { arr = hexa_array_push(arr, hexa_str(pos)); break; }
        *found = 0;
        arr = hexa_array_push(arr, hexa_str(pos));
        pos = found + dlen;
    }
    free(src);
    return arr;
}

// ─────────────────────────────────────────────────────────────────────
// M1-lite (hxa-20260423-003 Step 4): rt_str_* layer — GENERATED from
// self/runtime_hi.hexa via tool/extract_runtime_hi.sh. Included below
// in the hexa_str_join region (where rt_str_* dependencies are ready).
// hexa_str_* shims forward to rt_str_* after the include.
// ─────────────────────────────────────────────────────────────────────

// M1 full · str_ext Step 5 (hxa-20260423-003): rt_str_trim — codegen
// emits rt_str_* directly; hexa_str_trim shim retired.
HexaVal rt_str_trim(HexaVal s) {
    if (!HX_IS_STR(s)) return s;
    char* str = HX_STR(s);
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;
    int len = strlen(str);
    while (len > 0 && (str[len-1] == ' ' || str[len-1] == '\t' || str[len-1] == '\n' || str[len-1] == '\r')) len--;
    char* result = strndup(str, len);
    return hexa_str_own(result);
}

HexaVal hexa_str_replace(HexaVal s, HexaVal old, HexaVal new_s) {
    if (!HX_IS_STR(s)) return s;
    size_t cap = strlen(HX_STR(s)) * 2 + 1;
    char* result = malloc(cap);
    size_t total = 0;
    result[0] = 0;
    char* pos = HX_STR(s);
    int oldlen = strlen(HX_STR(old));
    size_t newlen = strlen(HX_STR(new_s));
    while (1) {
        char* found = strstr(pos, HX_STR(old));
        if (!found) {
            size_t rlen = strlen(pos);
            while (total + rlen + 1 > cap) { cap *= 2; result = realloc(result, cap); }
            memcpy(result + total, pos, rlen);
            total += rlen;
            result[total] = 0;
            break;
        }
        size_t seg = (size_t)(found - pos);
        while (total + seg + newlen + 1 > cap) { cap *= 2; result = realloc(result, cap); }
        memcpy(result + total, pos, seg);
        total += seg;
        memcpy(result + total, HX_STR(new_s), newlen);
        total += newlen;
        result[total] = 0;
        pos = found + oldlen;
    }
    return hexa_str_own(result);
}

// M1 full · str_ext Step 5 (hxa-20260423-003): rt_str_to_upper/lower —
// codegen emits rt_str_* directly; hexa_str_to_upper/lower shims retired.
HexaVal rt_str_to_upper(HexaVal s) {
    if (!HX_IS_STR(s)) return s;
    char* r = strdup(HX_STR(s));
    for (int i = 0; r[i]; i++) if (r[i] >= 'a' && r[i] <= 'z') r[i] -= 32;
    return hexa_str_own(r);
}

HexaVal rt_str_to_lower(HexaVal s) {
    if (!HX_IS_STR(s)) return s;
    char* r = strdup(HX_STR(s));
    for (int i = 0; r[i]; i++) if (r[i] >= 'A' && r[i] <= 'Z') r[i] += 32;
    return hexa_str_own(r);
}

HexaVal hexa_str_join(HexaVal arr, HexaVal sep) {
    if (!HX_IS_ARRAY(arr) || HX_ARR_LEN(arr) == 0) return hexa_str("");
    size_t total_size = 0;
    for (int i = 0; i < HX_ARR_LEN(arr); i++) {
        HexaVal s = hexa_to_string(HX_ARR_ITEMS(arr)[i]);
        total_size += strlen(HX_STR(s));
    }
    size_t seplen = strlen(HX_STR(sep));
    total_size += (HX_ARR_LEN(arr) - 1) * seplen;
    char* result = malloc(total_size + 1);
    size_t total = 0;
    for (int i = 0; i < HX_ARR_LEN(arr); i++) {
        if (i > 0) {
            memcpy(result + total, HX_STR(sep), seplen);
            total += seplen;
        }
        HexaVal s = hexa_to_string(HX_ARR_ITEMS(arr)[i]);
        size_t slen = strlen(HX_STR(s));
        memcpy(result + total, HX_STR(s), slen);
        total += slen;
    }
    result[total] = 0;
    return hexa_str_own(result);
}

// ─────────────────────────────────────────────────────────────────────
// M1-lite (hxa-20260423-003 Step 4): rt_str_* layer from runtime_hi.hexa
// SSOT. Must appear AFTER hexa_str_join (depended on by pad_left/pad_right/
// repeat/center) and AFTER hexa_str_substring (depended on by rt_str_split).
// Shims below forward the public hexa_str_* symbols to the generated
// rt_str_* implementations.
// ─────────────────────────────────────────────────────────────────────
#include "runtime_hi_gen.c"
// Step 5 (hxa-20260423-003): hexa_str_lines/repeat/pad_left/pad_right/center
// shims removed — codegen now dispatches directly to rt_str_* from
// runtime_hi_gen.c (SSOT: self/runtime_hi.hexa). See self/codegen_c2.hexa
// cg_string_sym "c" branch (lines 250-272).

static int utf8_cpcount(const char* s) {
    int n = 0;
    for (int i = 0; s[i]; i++) if ((s[i] & 0xC0) != 0x80) n++;
    return n;
}
HexaVal hexa_pad_left(HexaVal s, HexaVal width) {
    HexaVal str = hexa_to_string(s);
    int w = HX_INT(width);
    int cplen = utf8_cpcount(HX_STR(str));
    int bytelen = strlen(HX_STR(str));
    if (cplen >= w) return str;
    int pad = w - cplen;
    char* result = malloc(bytelen + pad + 1);
    memset(result, ' ', pad);
    strcpy(result + pad, HX_STR(str));
    return hexa_str_own(result);
}


// Bootstrap shim: hexa-level `join(arr, sep)` free-fn idiom in SSOT modules
// emits `hexa_call2(join, arr, sep)` via TAG_FN lookup. Once hexa_v2 is rebuilt
// from codegen_c2.hexa's join-builtin dispatch, this becomes unused.
// (`split` was retired 2026-04-21 — codegen now emits hexa_str_split directly.)
static HexaVal join;

HexaVal hexa_pad_right(HexaVal s, HexaVal width) {
    HexaVal str = hexa_to_string(s);
    int w = HX_INT(width);
    int cplen = utf8_cpcount(HX_STR(str));
    int bytelen = strlen(HX_STR(str));
    if (cplen >= w) return str;
    int pad = w - cplen;
    char* result = malloc(bytelen + pad + 1);
    strcpy(result, HX_STR(str));
    memset(result + bytelen, ' ', pad);
    result[bytelen + pad] = 0;
    return hexa_str_own(result);
}

// B-19: Polymorphic arithmetic — T39 routes through __hx_to_double.
// ROI-47: explicit float+float fast path avoids 2x __hx_to_double tag dispatch.
HexaVal hexa_sub(HexaVal a, HexaVal b) {
    if (HX_IS_INT(a) && HX_IS_INT(b)) return hexa_int(HX_INT(a) - HX_INT(b));
    if (HX_IS_FLOAT(a) && HX_IS_FLOAT(b)) return hexa_float(HX_FLOAT(a) - HX_FLOAT(b));
    return hexa_float(__hx_to_double(a) - __hx_to_double(b));
}
HexaVal hexa_mul(HexaVal a, HexaVal b) {
    if (HX_IS_INT(a) && HX_IS_INT(b)) return hexa_int(HX_INT(a) * HX_INT(b));
    if (HX_IS_FLOAT(a) && HX_IS_FLOAT(b)) return hexa_float(HX_FLOAT(a) * HX_FLOAT(b));
    return hexa_float(__hx_to_double(a) * __hx_to_double(b));
}
// FMA: fused multiply-add — fma(a,b,c) = a*b + c
// For floats uses C99 fma() (single rounding, hardware FMA on modern CPUs).
// For pure ints falls back to a*b+c with int arithmetic.
HexaVal hexa_fma(HexaVal a, HexaVal b, HexaVal c) {
    if (HX_IS_INT(a) && HX_IS_INT(b) && HX_IS_INT(c))
        return hexa_int(HX_INT(a) * HX_INT(b) + HX_INT(c));
    return hexa_float(fma(__hx_to_double(a), __hx_to_double(b), __hx_to_double(c)));
}
HexaVal hexa_div(HexaVal a, HexaVal b) {
    if (HX_IS_INT(a) && HX_IS_INT(b)) {
        // 이전엔 silent 0 반환 → 버그 은폐. interp 는 이미 throw + void.
        // hexa_throw 는 try-stack 이 있으면 longjmp, 없으면 stderr + exit.
        if (HX_INT(b) == 0) { hexa_throw(hexa_str("division by zero")); return hexa_int(0); }
        return hexa_int(HX_INT(a) / HX_INT(b));
    }
    if (HX_IS_FLOAT(a) && HX_IS_FLOAT(b)) {
        if (HX_FLOAT(b) == 0.0) { hexa_throw(hexa_str("division by zero")); return hexa_float(0.0); }
        return hexa_float(HX_FLOAT(a) / HX_FLOAT(b));
    }
    double fb = __hx_to_double(b);
    if (fb == 0.0) { hexa_throw(hexa_str("division by zero")); return hexa_float(0.0); }
    return hexa_float(__hx_to_double(a) / fb);
}
HexaVal hexa_mod(HexaVal a, HexaVal b) {
    if (HX_IS_INT(a) && HX_IS_INT(b)) {
        if (HX_INT(b) == 0) { hexa_throw(hexa_str("modulo by zero")); return hexa_int(0); }
        return hexa_int(HX_INT(a) % HX_INT(b));
    }
    double fb = __hx_to_double(b);
    if (fb == 0.0) { hexa_throw(hexa_str("modulo by zero")); return hexa_float(0.0); }
    return hexa_float(fmod(__hx_to_double(a), fb));
}

// ROI-44: comparison runtime helpers — replace inline GCC stmt-expr in codegen.
// Semantics: TAG_FLOAT promotes to double compare; TAG_INT uses int64 compare.
// 2026-04-17: TAG_VALSTRUCT (interpreter Val wrapper) routes through
// __hx_to_double so wrapped ints compare on the inner value rather than
// HX_INT(v) reading the .vs pointer as garbage. Symptom before fix:
// `let x = 5; let z = 1 + x` returned 0xA00000005 because val_int's cache
// range check `n < __VAL_INT_CACHE_LEN` mis-evaluated when one side was
// a wrapped Val from the cached singleton table.
// String comparisons via strcmp — 이전 경로는 __hx_to_double("abc") = atof
// 으로 fallthrough 해서 대부분 0.0 으로 수렴 + intern pointer 비교로 떨어져
// 의미 없는 순서 (abc>abb=false 같은 오류).
HexaVal hexa_cmp_lt(HexaVal a, HexaVal b) {
    if (HX_IS_STR(a) && HX_IS_STR(b))
        return hexa_bool(strcmp(HX_STR(a), HX_STR(b)) < 0);
    if (HX_IS_FLOAT(a) || HX_IS_FLOAT(b) || HX_IS_VALSTRUCT(a) || HX_IS_VALSTRUCT(b))
        return hexa_bool(__hx_to_double(a) < __hx_to_double(b));
    return hexa_bool(HX_INT(a) < HX_INT(b));
}
HexaVal hexa_cmp_gt(HexaVal a, HexaVal b) {
    if (HX_IS_STR(a) && HX_IS_STR(b))
        return hexa_bool(strcmp(HX_STR(a), HX_STR(b)) > 0);
    if (HX_IS_FLOAT(a) || HX_IS_FLOAT(b) || HX_IS_VALSTRUCT(a) || HX_IS_VALSTRUCT(b))
        return hexa_bool(__hx_to_double(a) > __hx_to_double(b));
    return hexa_bool(HX_INT(a) > HX_INT(b));
}
HexaVal hexa_cmp_le(HexaVal a, HexaVal b) {
    if (HX_IS_STR(a) && HX_IS_STR(b))
        return hexa_bool(strcmp(HX_STR(a), HX_STR(b)) <= 0);
    if (HX_IS_FLOAT(a) || HX_IS_FLOAT(b) || HX_IS_VALSTRUCT(a) || HX_IS_VALSTRUCT(b))
        return hexa_bool(__hx_to_double(a) <= __hx_to_double(b));
    return hexa_bool(HX_INT(a) <= HX_INT(b));
}
HexaVal hexa_cmp_ge(HexaVal a, HexaVal b) {
    if (HX_IS_STR(a) && HX_IS_STR(b))
        return hexa_bool(strcmp(HX_STR(a), HX_STR(b)) >= 0);
    if (HX_IS_FLOAT(a) || HX_IS_FLOAT(b) || HX_IS_VALSTRUCT(a) || HX_IS_VALSTRUCT(b))
        return hexa_bool(__hx_to_double(a) >= __hx_to_double(b));
    return hexa_bool(HX_INT(a) >= HX_INT(b));
}

int hexa_array_contains(HexaVal arr, HexaVal item) {
    if (!HX_IS_ARRAY(arr)) return 0;
    for (int i = 0; i < HX_ARR_LEN(arr); i++) {
        if (hexa_truthy(hexa_eq(HX_ARR_ITEMS(arr)[i], item))) return 1;
    }
    return 0;
}

// count_substr(s, substr): number of non-overlapping occurrences.
// Matches interpreter's greedy advance (hexa_full.hexa:14954-14973).
HexaVal hexa_str_count_substr(HexaVal s, HexaVal sub) {
    if (!HX_IS_STR(s) || !HX_IS_STR(sub)) return hexa_int(0);
    const char* src = HX_STR(s);
    const char* pat = HX_STR(sub);
    int plen = (int)strlen(pat);
    if (plen == 0) return hexa_int(0);
    int64_t cnt = 0;
    const char* p = src;
    while ((p = strstr(p, pat)) != NULL) {
        cnt++;
        p += plen; // non-overlapping
    }
    return hexa_int(cnt);
}

HexaVal hexa_format_float(HexaVal f, HexaVal prec) {
    double v = __hx_to_double(f);
    int p = HX_INT(prec);
    char buf[64];
    snprintf(buf, 64, "%.*f", p, v);
    return hexa_str(buf);
}

HexaVal hexa_format_float_sci(HexaVal f, HexaVal prec) {
    double v = __hx_to_double(f);
    int p = HX_INT(prec);
    char buf[64];
    snprintf(buf, 64, "%.*e", p, v);
    return hexa_str(buf);
}

// ── Extern FFI: dlopen / dlsym / dispatch ───────────────

// Resolve a library path for dlopen.
// If lib_name is NULL or empty, use RTLD_DEFAULT (search default symbols).
// Helper: extract the basename (without "lib" prefix and without
// ".dylib"/".so" suffix) from a possibly-absolute library path. Used so
// `@link("/abs/path/libhxblas.dylib")` can fall back to a Linux
// `libhxblas.so` (or vice-versa) without source changes — see the
// hxblas-linux port (2026-04-16, c6_hxblas_linux_port_20260416.json).
//
// Returns 1 on success and writes the extracted name into out_name (max
// out_cap bytes). Returns 0 if the input doesn't look like an
// absolute lib path.
static int hexa_ffi_extract_libname(const char* path, char* out_name, size_t out_cap) {
    if (!path || !out_name || out_cap == 0) return 0;
    // Find the basename (after the last '/')
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    // Must start with "lib"
    if (strncmp(base, "lib", 3) != 0) return 0;
    base += 3;
    // Must end in .dylib or .so or .so.N
    size_t blen = strlen(base);
    const char* end = NULL;
    if (blen > 6 && strcmp(base + blen - 6, ".dylib") == 0) {
        end = base + blen - 6;
    } else if (blen > 3 && strcmp(base + blen - 3, ".so") == 0) {
        end = base + blen - 3;
    } else {
        // .so.N case — find ".so." substring
        const char* p = strstr(base, ".so.");
        if (p) end = p;
    }
    if (!end) return 0;
    size_t nlen = (size_t)(end - base);
    if (nlen == 0 || nlen >= out_cap) return 0;
    memcpy(out_name, base, nlen);
    out_name[nlen] = '\0';
    return 1;
}

static void* hexa_ffi_dlopen(const char* lib_name) {
    if (!lib_name || lib_name[0] == '\0') {
        return dlopen(NULL, RTLD_LAZY);
    }
#ifdef __APPLE__
    // Try framework path first
    char framework[512];
    snprintf(framework, sizeof(framework),
        "/System/Library/Frameworks/%s.framework/%s", lib_name, lib_name);
    void* h = dlopen(framework, RTLD_LAZY);
    if (h) return h;
    // Try dylib
    char dylib[512];
    snprintf(dylib, sizeof(dylib), "/usr/lib/lib%s.dylib", lib_name);
    h = dlopen(dylib, RTLD_LAZY);
    if (h) return h;
    // ── New (2026-04-16): also try `lib<name>.dylib` so a bare
    //    @link("hxblas") + DYLD_LIBRARY_PATH=<dir> works on Mac
    //    just like `lib<name>.so` + LD_LIBRARY_PATH does on Linux. ──
    char dylib_bare[512];
    snprintf(dylib_bare, sizeof(dylib_bare), "lib%s.dylib", lib_name);
    h = dlopen(dylib_bare, RTLD_LAZY);
    if (h) return h;
    // ── C2 Step 3 (2026-04-16): macOS SIP strips DYLD_LIBRARY_PATH
    //    when crossing any system-signed binary (/bin/sh, etc). So
    //    `./hexa run` → popen → sh -c → interp loses DYLD. Work around
    //    by searching a few known repo-relative install dirs for the
    //    hexa-lang native build output before giving up. ──
    {
        const char* search_prefixes[8];
        int sp_n = 0;
        const char* hl = getenv("HEXA_LANG");
        if (hl && hl[0]) search_prefixes[sp_n++] = hl;
        const char* nld = getenv("HEXA_NATIVE_LIB_DIR");
        if (nld && nld[0]) search_prefixes[sp_n++] = nld;
        search_prefixes[sp_n++] = ".";
        search_prefixes[sp_n++] = "/Users/ghost/Dev/hexa-lang";
        search_prefixes[sp_n++] = "/Users/ghost/dev/hexa-lang";
        // Try <prefix>/self/native/build/lib<name>.dylib for each prefix.
        const char* rel_patterns[] = {
            "%s/self/native/build/lib%s.dylib",
            "%s/build/lib%s.dylib",
            "%s/lib%s.dylib",
            NULL
        };
        for (int i = 0; i < sp_n; i++) {
            for (int j = 0; rel_patterns[j]; j++) {
                char p[512];
                snprintf(p, sizeof(p), rel_patterns[j], search_prefixes[i], lib_name);
                void* hr = dlopen(p, RTLD_LAZY);
                if (hr) return hr;
            }
        }
    }
#endif
    // Try as-is (Linux .so or absolute path)
    char sopath[512];
    snprintf(sopath, sizeof(sopath), "lib%s.so", lib_name);
    void* h2 = dlopen(sopath, RTLD_LAZY);
    if (h2) return h2;
#ifndef __APPLE__
    // CUDA path + soname version fallbacks (Linux)
    {
        char path[512];
        const char* cuda_dirs[] = {
            "/usr/local/cuda/lib64/lib%s.so",
            "/usr/local/cuda/lib64/lib%s.so.12",
            "/usr/local/cuda/lib64/lib%s.so.11",
            NULL
        };
        for (int j = 0; cuda_dirs[j]; j++) {
            snprintf(path, sizeof(path), cuda_dirs[j], lib_name);
            void* hc = dlopen(path, RTLD_LAZY);
            if (hc) return hc;
        }
        const char* sonames[] = {"lib%s.so.12", "lib%s.so.11", NULL};
        for (int j = 0; sonames[j]; j++) {
            snprintf(path, sizeof(path), sonames[j], lib_name);
            void* hc = dlopen(path, RTLD_LAZY);
            if (hc) return hc;
        }
    }
#endif
    // ── New (2026-04-16, hxblas linux port): when the user passes
    //    an absolute Mac/Linux library path that doesn't resolve
    //    on this host (e.g. @link("/Users/.../libhxblas.dylib") on a
    //    Linux pod), strip the dirname + lib prefix + .{dylib,so}
    //    suffix and retry the search with just the base name. This
    //    lets the same hexa source resolve `libhxblas.so` on Linux
    //    via LD_LIBRARY_PATH and `libhxblas.dylib` on Mac via
    //    DYLD_LIBRARY_PATH without #ifdef in the hexa file. ──
    if (lib_name[0] == '/') {
        char base[256];
        if (hexa_ffi_extract_libname(lib_name, base, sizeof(base))) {
#ifdef __APPLE__
            char p[512];
            snprintf(p, sizeof(p), "lib%s.dylib", base);
            void* hb = dlopen(p, RTLD_LAZY);
            if (hb) return hb;
#else
            char p[512];
            snprintf(p, sizeof(p), "lib%s.so", base);
            void* hb = dlopen(p, RTLD_LAZY);
            if (hb) return hb;
#endif
            // Also honour HEXA_NATIVE_LIB_DIR explicit search prefix
            const char* dir = getenv("HEXA_NATIVE_LIB_DIR");
            if (dir && dir[0]) {
                char p2[512];
#ifdef __APPLE__
                snprintf(p2, sizeof(p2), "%s/lib%s.dylib", dir, base);
#else
                snprintf(p2, sizeof(p2), "%s/lib%s.so", dir, base);
#endif
                void* hb2 = dlopen(p2, RTLD_LAZY);
                if (hb2) return hb2;
            }
        }
    }
    // Final attempt: bare name
    return dlopen(lib_name, RTLD_LAZY);
}

// Resolve a symbol from an already-opened library handle.
static void* hexa_ffi_dlsym(void* handle, const char* symbol) {
    void* sym = dlsym(handle, symbol);
    if (!sym) {
        fprintf(stderr, "[hexa-ffi] dlsym failed for '%s': %s\n", symbol, dlerror());
    }
    return sym;
}

// Marshal a HexaVal to a raw i64 for C ABI passing.
static int64_t hexa_ffi_marshal_arg(HexaVal v) {
    switch (HX_TAG(v)) {
        case TAG_INT:   return HX_INT(v);
        case TAG_FLOAT: { union { double d; int64_t i; } u; u.d = HX_FLOAT(v); return u.i; }
        case TAG_BOOL:  return (int64_t)HX_BOOL(v);
        case TAG_STR:   return (int64_t)(uintptr_t)HX_STR(v);
        case TAG_VOID:  return 0;
        default:        return 0;
    }
}

// Raw extern call dispatch — supports 0 to 14 arguments.
// Extended 2026-04-16 from 12 to 14 to support cblas_sgemm (14 args).
// Uses transmute (cast) to call through a function pointer with the right arity.
static int64_t hexa_call_extern_raw(void* fn_ptr, int64_t* args, int nargs) {
    typedef int64_t (*Fn0)(void);
    typedef int64_t (*Fn1)(int64_t);
    typedef int64_t (*Fn2)(int64_t,int64_t);
    typedef int64_t (*Fn3)(int64_t,int64_t,int64_t);
    typedef int64_t (*Fn4)(int64_t,int64_t,int64_t,int64_t);
    typedef int64_t (*Fn5)(int64_t,int64_t,int64_t,int64_t,int64_t);
    typedef int64_t (*Fn6)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t);
    typedef int64_t (*Fn7)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t);
    typedef int64_t (*Fn8)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t);
    typedef int64_t (*Fn9)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t);
    typedef int64_t (*Fn10)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t);
    typedef int64_t (*Fn11)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t);
    typedef int64_t (*Fn12)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t);
    typedef int64_t (*Fn13)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t);
    typedef int64_t (*Fn14)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t);

    switch (nargs) {
        case 0:  return ((Fn0)fn_ptr)();
        case 1:  return ((Fn1)fn_ptr)(args[0]);
        case 2:  return ((Fn2)fn_ptr)(args[0],args[1]);
        case 3:  return ((Fn3)fn_ptr)(args[0],args[1],args[2]);
        case 4:  return ((Fn4)fn_ptr)(args[0],args[1],args[2],args[3]);
        case 5:  return ((Fn5)fn_ptr)(args[0],args[1],args[2],args[3],args[4]);
        case 6:  return ((Fn6)fn_ptr)(args[0],args[1],args[2],args[3],args[4],args[5]);
        case 7:  return ((Fn7)fn_ptr)(args[0],args[1],args[2],args[3],args[4],args[5],args[6]);
        case 8:  return ((Fn8)fn_ptr)(args[0],args[1],args[2],args[3],args[4],args[5],args[6],args[7]);
        case 9:  return ((Fn9)fn_ptr)(args[0],args[1],args[2],args[3],args[4],args[5],args[6],args[7],args[8]);
        case 10: return ((Fn10)fn_ptr)(args[0],args[1],args[2],args[3],args[4],args[5],args[6],args[7],args[8],args[9]);
        case 11: return ((Fn11)fn_ptr)(args[0],args[1],args[2],args[3],args[4],args[5],args[6],args[7],args[8],args[9],args[10]);
        case 12: return ((Fn12)fn_ptr)(args[0],args[1],args[2],args[3],args[4],args[5],args[6],args[7],args[8],args[9],args[10],args[11]);
        case 13: return ((Fn13)fn_ptr)(args[0],args[1],args[2],args[3],args[4],args[5],args[6],args[7],args[8],args[9],args[10],args[11],args[12]);
        case 14: return ((Fn14)fn_ptr)(args[0],args[1],args[2],args[3],args[4],args[5],args[6],args[7],args[8],args[9],args[10],args[11],args[12],args[13]);
        default:
            fprintf(stderr, "[hexa-ffi] extern call with %d args not supported (max 14)\n", nargs);
            return 0;
    }
}

// High-level extern call: resolve, marshal, call, unmarshal.
// ret_kind: 0=void, 1=int, 2=float, 3=bool, 4=pointer/string
HexaVal hexa_extern_call(void* fn_ptr, HexaVal* hargs, int nargs, int ret_kind) {
    int64_t cargs[14];  // extended for cblas_sgemm (14 args)
    for (int i = 0; i < nargs && i < 14; i++) {
        cargs[i] = hexa_ffi_marshal_arg(hargs[i]);
    }
    int64_t ret = hexa_call_extern_raw(fn_ptr, cargs, nargs);
    switch (ret_kind) {
        case 0:  return hexa_void();
        case 1:  return hexa_int(ret);
        case 2:  { union { int64_t i; double d; } u; u.i = ret; return hexa_float(u.d); }
        case 3:  return hexa_bool(ret != 0);
        case 4:  return hexa_int(ret); // pointer as int
        default: return hexa_int(ret);
    }
}

// ── G3: Typed extern call with float support ─────────────
// float_mask is a bitmask: bit i set means arg i is a double (SIMD register).
// This generates correct ARM64 calling convention for mixed int/float args.
// Supports up to 14 args (covers vDSP_mmul 9-arg, cblas_sgemm 14-arg).
// The C compiler sees typed args and emits correct register moves.
HexaVal hexa_extern_call_typed(void* fn_ptr, HexaVal* hargs, int nargs, int ret_kind, int float_mask) {
    // Extract values: doubles from .f, integers from .i
    double  fv[14]; // float values
    int64_t iv[14]; // int values
    for (int i = 0; i < nargs && i < 14; i++) {
        if (float_mask & (1 << i)) {
            fv[i] = HX_IS_FLOAT(hargs[i]) ? HX_FLOAT(hargs[i]) : (double)HX_INT(hargs[i]);
        } else {
            iv[i] = hexa_ffi_marshal_arg(hargs[i]);
        }
    }
    // Dispatch by nargs and float_mask pattern
    // Common patterns: all-int (mask=0), trailing floats, all-float
    // For arbitrary patterns, we use a switch on nargs with typed casts.
    // The C compiler handles register allocation from the typed call.

    // Helper macros for arg extraction
    #define A(i) ((float_mask & (1<<(i))) ? 0 : iv[i])
    #define F(i) ((float_mask & (1<<(i))) ? fv[i] : 0.0)
    #define IS_F(i) (float_mask & (1<<(i)))

    int64_t reti = 0;
    double  retf = 0.0;
    int ret_is_float = (ret_kind == 2);

    // For each arity, generate all-int call if mask==0, else use typed dispatch
    switch (nargs) {
        case 0: {
            if (ret_is_float) { retf = ((double(*)(void))fn_ptr)(); }
            else { reti = ((int64_t(*)(void))fn_ptr)(); }
            break;
        }
        case 1: {
            if (IS_F(0)) {
                if (ret_is_float) retf = ((double(*)(double))fn_ptr)(fv[0]);
                else reti = ((int64_t(*)(double))fn_ptr)(fv[0]);
            } else {
                if (ret_is_float) retf = ((double(*)(int64_t))fn_ptr)(iv[0]);
                else reti = ((int64_t(*)(int64_t))fn_ptr)(iv[0]);
            }
            break;
        }
        case 2: {
            // 4 combinations for 2 args
            int p = (IS_F(0)?2:0) | (IS_F(1)?1:0);
            switch (p) {
                case 0: // ii
                    if (ret_is_float) retf = ((double(*)(int64_t,int64_t))fn_ptr)(iv[0],iv[1]);
                    else reti = ((int64_t(*)(int64_t,int64_t))fn_ptr)(iv[0],iv[1]);
                    break;
                case 1: // if
                    if (ret_is_float) retf = ((double(*)(int64_t,double))fn_ptr)(iv[0],fv[1]);
                    else reti = ((int64_t(*)(int64_t,double))fn_ptr)(iv[0],fv[1]);
                    break;
                case 2: // fi
                    if (ret_is_float) retf = ((double(*)(double,int64_t))fn_ptr)(fv[0],iv[1]);
                    else reti = ((int64_t(*)(double,int64_t))fn_ptr)(fv[0],iv[1]);
                    break;
                case 3: // ff
                    if (ret_is_float) retf = ((double(*)(double,double))fn_ptr)(fv[0],fv[1]);
                    else reti = ((int64_t(*)(double,double))fn_ptr)(fv[0],fv[1]);
                    break;
            }
            break;
        }
        case 3: {
            // 3 args — dispatch 2^3 = 8 patterns for int/float lane mapping.
            int p = (IS_F(0)?4:0) | (IS_F(1)?2:0) | (IS_F(2)?1:0);
            switch (p) {
                case 0: if (ret_is_float) retf = ((double(*)(int64_t,int64_t,int64_t))fn_ptr)(iv[0],iv[1],iv[2]); else reti = ((int64_t(*)(int64_t,int64_t,int64_t))fn_ptr)(iv[0],iv[1],iv[2]); break;
                case 1: if (ret_is_float) retf = ((double(*)(int64_t,int64_t,double))fn_ptr)(iv[0],iv[1],fv[2]); else reti = ((int64_t(*)(int64_t,int64_t,double))fn_ptr)(iv[0],iv[1],fv[2]); break;
                case 2: if (ret_is_float) retf = ((double(*)(int64_t,double,int64_t))fn_ptr)(iv[0],fv[1],iv[2]); else reti = ((int64_t(*)(int64_t,double,int64_t))fn_ptr)(iv[0],fv[1],iv[2]); break;
                case 3: if (ret_is_float) retf = ((double(*)(int64_t,double,double))fn_ptr)(iv[0],fv[1],fv[2]); else reti = ((int64_t(*)(int64_t,double,double))fn_ptr)(iv[0],fv[1],fv[2]); break;
                case 4: if (ret_is_float) retf = ((double(*)(double,int64_t,int64_t))fn_ptr)(fv[0],iv[1],iv[2]); else reti = ((int64_t(*)(double,int64_t,int64_t))fn_ptr)(fv[0],iv[1],iv[2]); break;
                case 5: if (ret_is_float) retf = ((double(*)(double,int64_t,double))fn_ptr)(fv[0],iv[1],fv[2]); else reti = ((int64_t(*)(double,int64_t,double))fn_ptr)(fv[0],iv[1],fv[2]); break;
                case 6: if (ret_is_float) retf = ((double(*)(double,double,int64_t))fn_ptr)(fv[0],fv[1],iv[2]); else reti = ((int64_t(*)(double,double,int64_t))fn_ptr)(fv[0],fv[1],iv[2]); break;
                case 7: if (ret_is_float) retf = ((double(*)(double,double,double))fn_ptr)(fv[0],fv[1],fv[2]); else reti = ((int64_t(*)(double,double,double))fn_ptr)(fv[0],fv[1],fv[2]); break;
            }
            break;
        }
        case 4: {
            // 4 args — only handle common trailing-float patterns explicitly.
            int p = (IS_F(0)?8:0) | (IS_F(1)?4:0) | (IS_F(2)?2:0) | (IS_F(3)?1:0);
            if (p == 0) {
                if (ret_is_float) retf = ((double(*)(int64_t,int64_t,int64_t,int64_t))fn_ptr)(iv[0],iv[1],iv[2],iv[3]);
                else reti = ((int64_t(*)(int64_t,int64_t,int64_t,int64_t))fn_ptr)(iv[0],iv[1],iv[2],iv[3]);
            } else if (p == 1) { // iiif
                if (ret_is_float) retf = ((double(*)(int64_t,int64_t,int64_t,double))fn_ptr)(iv[0],iv[1],iv[2],fv[3]);
                else reti = ((int64_t(*)(int64_t,int64_t,int64_t,double))fn_ptr)(iv[0],iv[1],iv[2],fv[3]);
            } else {
                fprintf(stderr, "[hexa-ffi] unsupported 4-arg float pattern p=%d\n", p);
                reti = 0;
            }
            break;
        }
        case 5: {
            // 5 args — hxlayer_rmsnorm_silu(N:int, out:*, x:*, g:*, eps:f32)
            // is the driving case: (int,int,int,int,float) = p==1.
            int p = (IS_F(0)?16:0) | (IS_F(1)?8:0) | (IS_F(2)?4:0) | (IS_F(3)?2:0) | (IS_F(4)?1:0);
            if (p == 0) {
                if (ret_is_float) retf = ((double(*)(int64_t,int64_t,int64_t,int64_t,int64_t))fn_ptr)(iv[0],iv[1],iv[2],iv[3],iv[4]);
                else reti = ((int64_t(*)(int64_t,int64_t,int64_t,int64_t,int64_t))fn_ptr)(iv[0],iv[1],iv[2],iv[3],iv[4]);
            } else if (p == 1) { // iiiif
                if (ret_is_float) retf = ((double(*)(int64_t,int64_t,int64_t,int64_t,double))fn_ptr)(iv[0],iv[1],iv[2],iv[3],fv[4]);
                else reti = ((int64_t(*)(int64_t,int64_t,int64_t,int64_t,double))fn_ptr)(iv[0],iv[1],iv[2],iv[3],fv[4]);
            } else {
                fprintf(stderr, "[hexa-ffi] unsupported 5-arg float pattern p=%d\n", p);
                reti = 0;
            }
            break;
        }
        case 6: {
            // 6 args — cblas_saxpy(N, alpha, x, incx, y, incy)
            if (float_mask == 0) {
                reti = ((int64_t(*)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t))fn_ptr)(iv[0],iv[1],iv[2],iv[3],iv[4],iv[5]);
            } else if (float_mask == 0x02) { // i f i i i i — cblas_saxpy(N,alpha,x,incx,y,incy)
                ((void(*)(int64_t,double,int64_t,int64_t,int64_t,int64_t))fn_ptr)(iv[0],fv[1],iv[2],iv[3],iv[4],iv[5]);
                reti = 0;
            } else {
                reti = hexa_call_extern_raw(fn_ptr, iv, nargs);
            }
            break;
        }
        case 7: {
            // 7 args — vDSP_vadd/vmul(A,sA,B,sB,C,sC,N)
            if (float_mask == 0) {
                ((void(*)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t))fn_ptr)(iv[0],iv[1],iv[2],iv[3],iv[4],iv[5],iv[6]);
                reti = 0;
            } else {
                reti = hexa_call_extern_raw(fn_ptr, iv, nargs);
            }
            break;
        }
        case 8: {
            // 8 args
            if (float_mask == 0) {
                reti = ((int64_t(*)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t))fn_ptr)(iv[0],iv[1],iv[2],iv[3],iv[4],iv[5],iv[6],iv[7]);
            } else {
                reti = hexa_call_extern_raw(fn_ptr, iv, nargs);
            }
            break;
        }
        case 9: {
            // 9 args — vDSP_mmul(A,sA,B,sB,C,sC,M,N,K)
            if (float_mask == 0) {
                ((void(*)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t))fn_ptr)(iv[0],iv[1],iv[2],iv[3],iv[4],iv[5],iv[6],iv[7],iv[8]);
                reti = 0;
            } else {
                reti = hexa_call_extern_raw(fn_ptr, iv, nargs);
            }
            break;
        }
        default: {
            // 10-14 args: fall back to raw int dispatch (handles up to 14).
            reti = hexa_call_extern_raw(fn_ptr, iv, nargs);
            break;
        }
    }
    #undef A
    #undef F
    #undef IS_F

    switch (ret_kind) {
        case 0:  return hexa_void();
        case 1:  return hexa_int(reti);
        case 2:  return hexa_float(retf);
        case 3:  return hexa_bool(reti != 0);
        case 4:  return hexa_int(reti);
        default: return hexa_int(reti);
    }
}

// Convenience: cstring <-> HexaVal
HexaVal hexa_cstring(HexaVal s) {
    if (!HX_IS_STR(s)) return hexa_int(0);
    return hexa_int((int64_t)(uintptr_t)HX_STR(s));
}

HexaVal hexa_from_cstring(HexaVal ptr) {
    uint64_t p = HX_IS_INT(ptr) ? HX_INT_U(ptr) : 0;
    if (p == 0) return hexa_str("");
    return hexa_str((const char*)(uintptr_t)p);
}

HexaVal hexa_ptr_null() { return hexa_int(0); }

HexaVal hexa_ptr_addr(HexaVal v) {
    return hexa_int((int64_t)HX_INT_U(v));
}

// ── C2 Step 3: Dynamic FFI host dispatch (interpreter path) ──
//
// Purpose: expose dlopen+dlsym+extern_call as HexaVal builtins so the
// self-host interpreter (self/interpreter.hexa) can dispatch @link externs
// at runtime without going through codegen_c2.hexa's static __ffi_sym_*
// registration. The native (compile-to-native) path already works via
// codegen; this reopens the same pipeline for `./hexa run`.
//
// Forward declaration — defined alongside hexa_host_ffi_call below.
static HexaVal hexa_host_ffi_unwrap(HexaVal v);

// hexa_host_ffi_open(lib_name: str) -> ptr(int)
//   0 on failure (e.g. library not found under any search strategy).
HexaVal hexa_host_ffi_open(HexaVal lib_name) {
    HexaVal uv = hexa_host_ffi_unwrap(lib_name);
    const char* name = (HX_IS_STR(uv) && HX_STR(uv)) ? HX_STR(uv) : "";
    void* h = hexa_ffi_dlopen(name);
    return hexa_int((int64_t)(uintptr_t)h);
}

// hexa_host_ffi_sym(handle: int, symbol: str) -> ptr(int)
//   0 on failure.
HexaVal hexa_host_ffi_sym(HexaVal handle, HexaVal symbol) {
    HexaVal uh = hexa_host_ffi_unwrap(handle);
    HexaVal us = hexa_host_ffi_unwrap(symbol);
    void* h = HX_IS_INT(uh) ? (void*)(uintptr_t)HX_INT_U(uh) : NULL;
    const char* sym = (HX_IS_STR(us) && HX_STR(us)) ? HX_STR(us) : "";
    if (!h || !sym || !*sym) return hexa_int(0);
    void* fn = dlsym(h, sym);
    if (!fn) {
        fprintf(stderr, "[hexa-ffi] dlsym failed for '%s': %s\n", sym, dlerror());
        return hexa_int(0);
    }
    return hexa_int((int64_t)(uintptr_t)fn);
}

// hexa_host_ffi_call(fn_ptr: int, args_arr: array, float_mask: int, ret_kind: int) -> int|float
//   ret_kind: 0=void, 1=int, 2=float, 3=bool, 4=pointer
//   float_mask: bit i set => arg i is double
//
// Unwraps any HexaValStruct-wrapped args (the interpreter's native Val
// representation) down to the corresponding primitive HexaVal so that
// hexa_ffi_marshal_arg / hexa_extern_call_typed read the right lane.
// Without this the interpreter-produced TAG_VALSTRUCT falls through the
// marshal switch default and every arg becomes 0.
static HexaVal hexa_host_ffi_unwrap(HexaVal v) {
    if (!HX_IS_VALSTRUCT(v) || !HX_VS(v)) return v;
    int64_t t = HX_VSF(v, tag_i);
    // Hexa-level tag values: 0=INT, 1=FLOAT, 2=BOOL, 3=STR, 8=VOID, 13=POINTER
    // See self/interpreter.hexa let TAG_* constants.
    if (t == 0)  return hexa_int(HX_VSF(v, int_val));
    if (t == 1)  return hexa_float(HX_VSF(v, float_val));
    if (t == 2)  return hexa_bool(HX_VSF(v, bool_val));
    if (t == 3)  return HX_VSF(v, str_val);       // already TAG_STR
    if (t == 13) return hexa_int(HX_VSF(v, int_val));  // pointer → int ABI lane
    return hexa_int(HX_VSF(v, int_val));
}

HexaVal hexa_host_ffi_call(HexaVal fn_ptr, HexaVal args_arr, HexaVal float_mask, HexaVal ret_kind) {
    HexaVal fp_v = hexa_host_ffi_unwrap(fn_ptr);
    void* fp = HX_IS_INT(fp_v) ? (void*)(uintptr_t)HX_INT_U(fp_v) : NULL;
    if (!fp) return hexa_int(0);
    HexaVal rk_v = hexa_host_ffi_unwrap(ret_kind);
    HexaVal fm_v = hexa_host_ffi_unwrap(float_mask);
    int rk   = HX_IS_INT(rk_v) ? (int)HX_INT(rk_v) : 1;
    int mask = HX_IS_INT(fm_v) ? (int)HX_INT(fm_v) : 0;
    // args_arr may be (a) a TAG_ARRAY built by the native-compiled hexa
    // runtime (hexa_array_new — direct member access works) OR
    // (b) a TAG_VALSTRUCT whose tag_i == TAG_ARRAY (hexa level 5) — the
    // form used by the self-host interpreter's val_array(). In case (b)
    // the real data lives behind the hexa-level array_store, which this
    // native wrapper cannot reach. So we require native-array input and
    // error out otherwise.
    if (!HX_IS_ARRAY(args_arr)) {
        fprintf(stderr,
            "[hexa-ffi] host_ffi_call: expected native array (tag=%d), got tag=%d. "
            "Self-host interpreter must marshal through hexa_host_ffi_call_6 instead.\n",
            (int)TAG_ARRAY, (int)HX_TAG(args_arr));
        return hexa_int(0);
    }
    int nargs = hexa_len(args_arr);
    if (nargs > 12) nargs = 12;
    HexaVal hargs[12];
    for (int i = 0; i < nargs; i++) {
        hargs[i] = hexa_host_ffi_unwrap(hexa_array_get(args_arr, i));
    }
    if (mask) {
        return hexa_extern_call_typed(fp, hargs, nargs, rk, mask);
    }
    return hexa_extern_call(fp, hargs, nargs, rk);
}

// Static overflow buffer for >6 arg FFI calls.
// The interpreter stores overflow args here via magic fn_ptr sentinel
// values (-1, -2) before the real dispatch call.
static HexaVal g_ffi_overflow[14];

// hexa_host_ffi_call_6(fn_ptr, nargs, float_mask, ret_kind, a0, a1, a2, a3, a4, a5)
//   Scalar-fanout variant used by the self-host interpreter which
//   cannot pass its array_store-backed TAG_VALSTRUCT array through
//   hexa_array_get. Up to 6 positional args; unused slots are TAG_VOID.
//
//   Extended 2026-04-16: overflow protocol for >6 args (vDSP_mmul 9, cblas_sgemm 14).
//   Magic fn_ptr sentinels:
//     fn_ptr == -1 : store a0..a5 into g_ffi_overflow[0..5] (args 6-11)
//     fn_ptr == -2 : store a0..a1 into g_ffi_overflow[6..7] (args 12-13)
//   When fn_ptr is a real pointer and nargs > 6, read g_ffi_overflow for args 6+.
HexaVal hexa_host_ffi_call_6(
    HexaVal fn_ptr, HexaVal nargs_v, HexaVal float_mask, HexaVal ret_kind,
    HexaVal a0, HexaVal a1, HexaVal a2, HexaVal a3, HexaVal a4, HexaVal a5
) {
    HexaVal fp_v = hexa_host_ffi_unwrap(fn_ptr);
    int64_t fp_raw = HX_IS_INT(fp_v) ? HX_INT(fp_v) : 0;

    // Magic sentinel: store overflow args
    if (fp_raw == -1) {
        g_ffi_overflow[0] = hexa_host_ffi_unwrap(a0);
        g_ffi_overflow[1] = hexa_host_ffi_unwrap(a1);
        g_ffi_overflow[2] = hexa_host_ffi_unwrap(a2);
        g_ffi_overflow[3] = hexa_host_ffi_unwrap(a3);
        g_ffi_overflow[4] = hexa_host_ffi_unwrap(a4);
        g_ffi_overflow[5] = hexa_host_ffi_unwrap(a5);
        return hexa_int(0);
    }
    if (fp_raw == -2) {
        g_ffi_overflow[6] = hexa_host_ffi_unwrap(a0);
        g_ffi_overflow[7] = hexa_host_ffi_unwrap(a1);
        return hexa_int(0);
    }

    void* fp = (fp_raw != 0) ? (void*)(uintptr_t)fp_raw : NULL;
    if (!fp) return hexa_int(0);
    HexaVal na_v = hexa_host_ffi_unwrap(nargs_v);
    HexaVal rk_v = hexa_host_ffi_unwrap(ret_kind);
    HexaVal fm_v = hexa_host_ffi_unwrap(float_mask);
    int nargs = HX_IS_INT(na_v) ? (int)HX_INT(na_v) : 0;
    int rk    = HX_IS_INT(rk_v) ? (int)HX_INT(rk_v) : 1;
    int mask  = HX_IS_INT(fm_v) ? (int)HX_INT(fm_v) : 0;
    if (nargs < 0) nargs = 0;
    if (nargs > 14) nargs = 14;

    // Build hargs: first 6 from direct params, 6+ from overflow buffer
    HexaVal hargs[14];
    hargs[0] = hexa_host_ffi_unwrap(a0);
    hargs[1] = hexa_host_ffi_unwrap(a1);
    hargs[2] = hexa_host_ffi_unwrap(a2);
    hargs[3] = hexa_host_ffi_unwrap(a3);
    hargs[4] = hexa_host_ffi_unwrap(a4);
    hargs[5] = hexa_host_ffi_unwrap(a5);
    for (int i = 6; i < nargs; i++) {
        hargs[i] = g_ffi_overflow[i - 6];
    }

    HexaVal native_ret;
    if (mask) {
        native_ret = hexa_extern_call_typed(fp, hargs, nargs, rk, mask);
    } else {
        native_ret = hexa_extern_call(fp, hargs, nargs, rk);
    }
    // Wrap the native HexaVal (tag=TAG_INT / TAG_FLOAT / TAG_VOID / TAG_BOOL)
    // into a hexa-level Val so the self-host interpreter can .tag/.int_val
    // through it. Without this the interpreter reads garbage from the
    // HexaVal-as-map fallback in hexa_map_get.
    int64_t iv = 0;
    double  fv = 0.0;
    int bv = 0;
    if (HX_IS_INT(native_ret))   iv = HX_INT(native_ret);
    if (HX_IS_FLOAT(native_ret)) fv = HX_FLOAT(native_ret);
    if (HX_IS_BOOL(native_ret))  bv = HX_BOOL(native_ret);
    // Hexa-level tag constants: INT=0, FLOAT=1, BOOL=2, VOID=8.
    int hexa_tag = 0;
    if (rk == 2) hexa_tag = 1;
    if (rk == 3) hexa_tag = 2;
    if (rk == 0) hexa_tag = 8;
    HexaVal empty_str = hexa_str("");
    return hexa_valstruct_new_v(
        hexa_int(hexa_tag), hexa_int(iv), hexa_float(fv), hexa_bool(bv),
        empty_str, empty_str, empty_str,
        empty_str, empty_str, empty_str,
        empty_str, empty_str);
}

// ── G5: Pointer arithmetic builtins ─────────────────────

HexaVal hexa_ptr_alloc(HexaVal size) {
    int64_t n = HX_IS_INT(size) ? HX_INT(size) : 0;
    if (n <= 0) return hexa_int(0);
    void* p = calloc(1, (size_t)n);
    return hexa_int((int64_t)(uintptr_t)p);
}

HexaVal hexa_ptr_free(HexaVal ptr, HexaVal size) {
    uint64_t p = HX_IS_INT(ptr) ? HX_INT_U(ptr) : 0;
    if (p != 0) free((void*)(uintptr_t)p);
    return hexa_void();
}

HexaVal hexa_ptr_write(HexaVal ptr, HexaVal offset, HexaVal val) {
    uint64_t p = HX_IS_INT(ptr) ? HX_INT_U(ptr) : 0;
    int64_t off = HX_IS_INT(offset) ? HX_INT(offset) : 0;
    if (p == 0) return hexa_void();
    uint8_t* base = (uint8_t*)(uintptr_t)p + off;
    if (HX_IS_FLOAT(val)) {
        double d = HX_FLOAT(val);
        memcpy(base, &d, sizeof(double));
    } else {
        int64_t v = HX_INT(val);
        memcpy(base, &v, sizeof(int64_t));
    }
    return hexa_void();
}

/* @hot_kernel f32/f64/i32 ptr read/write — extracted to tensor_kernels.c
 * (included at end of this file). hexa_ptr_read (untyped 64-bit) kept here
 * as general-purpose. */

HexaVal hexa_ptr_read(HexaVal ptr, HexaVal offset) {
    uint64_t p = HX_IS_INT(ptr) ? HX_INT_U(ptr) : 0;
    int64_t off = HX_IS_INT(offset) ? HX_INT(offset) : 0;
    if (p == 0) return hexa_int(0);
    int64_t v;
    memcpy(&v, (uint8_t*)(uintptr_t)p + off, sizeof(int64_t));
    return hexa_int(v);
}

HexaVal hexa_ptr_offset(HexaVal ptr, HexaVal offset) {
    uint64_t p = HX_IS_INT(ptr) ? HX_INT_U(ptr) : 0;
    int64_t off = HX_IS_INT(offset) ? HX_INT(offset) : 0;
    return hexa_int((int64_t)(p + (uint64_t)off));
}

HexaVal hexa_deref(HexaVal ptr) {
    uint64_t p = HX_IS_INT(ptr) ? HX_INT_U(ptr) : 0;
    if (p == 0) return hexa_int(0);
    int64_t v;
    memcpy(&v, (void*)(uintptr_t)p, sizeof(int64_t));
    return hexa_int(v);
}

// ═══════════════════════════════════════════════════════════
//  G2: Struct Value Passing — struct packing builtins
//  Allows constructing C structs in heap memory for FFI.
//  struct_pack(v0, v1, ...) — N*8 bytes of f64 values
//  struct_pack_f32(v0, ...) — N*4 bytes of f32 values
//  struct_unpack(ptr, idx) — read Nth f64 from packed struct
//  struct_unpack_f32(ptr, idx) — read Nth f32
//  struct_rect/point/size — Cocoa geometry helpers
// ═══════════════════════════════════════════════════════════

// Pack N f64 values into a newly allocated buffer. Returns pointer as int.
HexaVal hexa_struct_pack(HexaVal* args, int nargs) {
    if (nargs <= 0) return hexa_int(0);
    size_t total = (size_t)nargs * sizeof(double);
    double* buf = (double*)calloc(1, total);
    for (int i = 0; i < nargs; i++) {
        buf[i] = HX_IS_FLOAT(args[i]) ? HX_FLOAT(args[i]) : (double)HX_INT(args[i]);
    }
    return hexa_int((int64_t)(uintptr_t)buf);
}

/* @hot_kernel hexa_struct_pack_f32 / hexa_struct_unpack_f32 extracted to
 * tensor_kernels.c (see include at end of this file). */

// Read the Nth f64 from a struct pointer.
HexaVal hexa_struct_unpack(HexaVal ptr, HexaVal index) {
    uint64_t p = HX_IS_INT(ptr) ? HX_INT_U(ptr) : 0;
    int64_t idx = HX_IS_INT(index) ? HX_INT(index) : 0;
    if (p == 0) return hexa_float(0.0);
    double* buf = (double*)(uintptr_t)p;
    return hexa_float(buf[idx]);
}

// Convenience: pack an NSRect (4x f64 = 32 bytes).
HexaVal hexa_struct_rect(HexaVal x, HexaVal y, HexaVal w, HexaVal h) {
    double* buf = (double*)calloc(1, 4 * sizeof(double));
    buf[0] = HX_IS_FLOAT(x) ? HX_FLOAT(x) : (double)HX_INT(x);
    buf[1] = HX_IS_FLOAT(y) ? HX_FLOAT(y) : (double)HX_INT(y);
    buf[2] = HX_IS_FLOAT(w) ? HX_FLOAT(w) : (double)HX_INT(w);
    buf[3] = HX_IS_FLOAT(h) ? HX_FLOAT(h) : (double)HX_INT(h);
    return hexa_int((int64_t)(uintptr_t)buf);
}

// Convenience: pack an NSPoint (2x f64 = 16 bytes).
HexaVal hexa_struct_point(HexaVal x, HexaVal y) {
    double* buf = (double*)calloc(1, 2 * sizeof(double));
    buf[0] = HX_IS_FLOAT(x) ? HX_FLOAT(x) : (double)HX_INT(x);
    buf[1] = HX_IS_FLOAT(y) ? HX_FLOAT(y) : (double)HX_INT(y);
    return hexa_int((int64_t)(uintptr_t)buf);
}

// Convenience: pack an NSSize (2x f64 = 16 bytes).
HexaVal hexa_struct_size_pack(HexaVal w, HexaVal h) {
    double* buf = (double*)calloc(1, 2 * sizeof(double));
    buf[0] = HX_IS_FLOAT(w) ? HX_FLOAT(w) : (double)HX_INT(w);
    buf[1] = HX_IS_FLOAT(h) ? HX_FLOAT(h) : (double)HX_INT(h);
    return hexa_int((int64_t)(uintptr_t)buf);
}

// Free a struct pointer (allocated by struct_pack/struct_rect/etc).
HexaVal hexa_struct_free(HexaVal ptr) {
    uint64_t p = HX_IS_INT(ptr) ? HX_INT_U(ptr) : 0;
    if (p != 0) free((void*)(uintptr_t)p);
    return hexa_void();
}

// ═══════════════════════════════════════════════════════════
//  G4: Callback Trampoline Pool
//  Provides C function pointers that dispatch back into hexa.
//  16 pre-allocated slots — sufficient for Cocoa delegates,
//  menu actions, timers, qsort comparators, etc.
// ═══════════════════════════════════════════════════════════

#define HEXA_TRAMPOLINE_POOL_SIZE 16

// Callback entry: stores a HexaVal function reference (TAG_FN)
// and metadata for the dispatch.
typedef struct {
    int in_use;
    HexaVal hexa_fn;        // The hexa function value (TAG_FN)
    void* fn_ptr;           // The C trampoline pointer for this slot
    int expected_argc;      // How many C args the trampoline receives
} HexaCallbackSlot;

static HexaCallbackSlot __hexa_cb_slots[HEXA_TRAMPOLINE_POOL_SIZE];

// User-supplied dispatch hook: the interpreter or compiled code sets this.
// Signature: dispatch(slot_id, argc, args[]) -> HexaVal
// For compiled code, this is wired at link time.
// For interpreter, the interpreter sets this to its own dispatcher.
typedef HexaVal (*hexa_cb_dispatch_fn)(int slot_id, int argc, int64_t* args);
static hexa_cb_dispatch_fn __hexa_cb_dispatcher = NULL;

// Internal dispatch: called by every trampoline.
// Converts raw C int64 args back to HexaVal and calls the registered function.
static int64_t hexa_trampoline_dispatch(int slot_id, int argc, int64_t* args) {
    if (slot_id < 0 || slot_id >= HEXA_TRAMPOLINE_POOL_SIZE) return 0;
    if (!__hexa_cb_slots[slot_id].in_use) return 0;

    // If a custom dispatcher is set (e.g. interpreter), use it
    if (__hexa_cb_dispatcher) {
        HexaVal result = __hexa_cb_dispatcher(slot_id, argc, args);
        if (HX_IS_INT(result)) return HX_INT(result);
        if (HX_IS_FLOAT(result)) { union { double d; int64_t i; } u; u.d = HX_FLOAT(result); return u.i; }
        return 0;
    }

    // For compiled code: call the fn_ptr stored in the slot directly.
    // Compiled hexa functions take HexaVal args and return HexaVal.
    // We wrap each raw int64 arg as hexa_int() before calling.
    HexaCallbackSlot* slot = &__hexa_cb_slots[slot_id];
    if (HX_IS_FN(slot->hexa_fn) && HX_FN_PTR(slot->hexa_fn)) {
        typedef HexaVal (*HFn0)(void);
        typedef HexaVal (*HFn1)(HexaVal);
        typedef HexaVal (*HFn2)(HexaVal, HexaVal);
        typedef HexaVal (*HFn3)(HexaVal, HexaVal, HexaVal);
        typedef HexaVal (*HFn4)(HexaVal, HexaVal, HexaVal, HexaVal);
        void* fp = HX_FN_PTR(slot->hexa_fn);
        HexaVal result;
        switch (argc) {
            case 0: result = ((HFn0)fp)(); break;
            case 1: result = ((HFn1)fp)(hexa_int(args[0])); break;
            case 2: result = ((HFn2)fp)(hexa_int(args[0]), hexa_int(args[1])); break;
            case 3: result = ((HFn3)fp)(hexa_int(args[0]), hexa_int(args[1]), hexa_int(args[2])); break;
            default: result = ((HFn4)fp)(hexa_int(args[0]), hexa_int(args[1]), hexa_int(args[2]), hexa_int(args[3])); break;
        }
        if (HX_IS_INT(result)) return HX_INT(result);
        if (HX_IS_FLOAT(result)) { union { double d; int64_t i; } u; u.d = HX_FLOAT(result); return u.i; }
        return 0;
    }
    return 0;
}

// ── 16 static trampoline functions ──────────────────────
// Each takes up to 4 int64_t args (covers qsort comparator, ObjC IMP,
// NSTimer callback, etc.). Extra args beyond 4 are passed as 0.
// For Cocoa IMP: (id self, SEL _cmd, ...) = 2-3 pointer-sized args.

#define TRAMP_DEF(N) \
static int64_t _hexa_tramp_##N(int64_t a0, int64_t a1, int64_t a2, int64_t a3) { \
    int64_t args[4] = {a0, a1, a2, a3}; \
    return hexa_trampoline_dispatch(N, 4, args); \
}

TRAMP_DEF(0)  TRAMP_DEF(1)  TRAMP_DEF(2)  TRAMP_DEF(3)
TRAMP_DEF(4)  TRAMP_DEF(5)  TRAMP_DEF(6)  TRAMP_DEF(7)
TRAMP_DEF(8)  TRAMP_DEF(9)  TRAMP_DEF(10) TRAMP_DEF(11)
TRAMP_DEF(12) TRAMP_DEF(13) TRAMP_DEF(14) TRAMP_DEF(15)

typedef int64_t (*tramp_fn_t)(int64_t, int64_t, int64_t, int64_t);

static tramp_fn_t __hexa_tramp_table[HEXA_TRAMPOLINE_POOL_SIZE] = {
    _hexa_tramp_0,  _hexa_tramp_1,  _hexa_tramp_2,  _hexa_tramp_3,
    _hexa_tramp_4,  _hexa_tramp_5,  _hexa_tramp_6,  _hexa_tramp_7,
    _hexa_tramp_8,  _hexa_tramp_9,  _hexa_tramp_10, _hexa_tramp_11,
    _hexa_tramp_12, _hexa_tramp_13, _hexa_tramp_14, _hexa_tramp_15
};

// Initialize trampoline pool (call once at startup)
static void hexa_trampoline_init(void) {
    for (int i = 0; i < HEXA_TRAMPOLINE_POOL_SIZE; i++) {
        __hexa_cb_slots[i].in_use = 0;
        __hexa_cb_slots[i].fn_ptr = (void*)__hexa_tramp_table[i];
        __hexa_cb_slots[i].hexa_fn = hexa_void();
        __hexa_cb_slots[i].expected_argc = 0;
    }
}

// ── Public API ──────────────────────────────────────────

// callback_create(hexa_fn_val) -> int (C function pointer as integer)
// Allocates a trampoline slot and returns the C function pointer.
HexaVal hexa_callback_create(HexaVal fn_val) {
    // Lazy init
    static int inited = 0;
    if (!inited) { hexa_trampoline_init(); inited = 1; }

    for (int i = 0; i < HEXA_TRAMPOLINE_POOL_SIZE; i++) {
        if (!__hexa_cb_slots[i].in_use) {
            __hexa_cb_slots[i].in_use = 1;
            __hexa_cb_slots[i].hexa_fn = fn_val;
            return hexa_int((int64_t)(uintptr_t)__hexa_cb_slots[i].fn_ptr);
        }
    }
    fprintf(stderr, "[hexa-callback] trampoline pool exhausted (%d slots)\n",
            HEXA_TRAMPOLINE_POOL_SIZE);
    return hexa_int(0);
}

// callback_free(ptr) -> void
// Frees the trampoline slot associated with the given C function pointer.
HexaVal hexa_callback_free(HexaVal ptr) {
    uint64_t p = HX_IS_INT(ptr) ? HX_INT_U(ptr) : 0;
    for (int i = 0; i < HEXA_TRAMPOLINE_POOL_SIZE; i++) {
        if ((uint64_t)(uintptr_t)__hexa_cb_slots[i].fn_ptr == p) {
            __hexa_cb_slots[i].in_use = 0;
            __hexa_cb_slots[i].hexa_fn = hexa_void();
            return hexa_void();
        }
    }
    return hexa_void();
}

// callback_slot_id(ptr) -> int
// Returns the slot index for a given trampoline pointer (-1 if not found).
HexaVal hexa_callback_slot_id(HexaVal ptr) {
    uint64_t p = HX_IS_INT(ptr) ? HX_INT_U(ptr) : 0;
    for (int i = 0; i < HEXA_TRAMPOLINE_POOL_SIZE; i++) {
        if ((uint64_t)(uintptr_t)__hexa_cb_slots[i].fn_ptr == p) {
            return hexa_int(i);
        }
    }
    return hexa_int(-1);
}

/* @hot_kernel B4 tensor stubs extracted to tensor_kernels.c (see include
 * at end of this file). clock/random stay here (OS utility, not hot). */

HexaVal hexa_clock(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return hexa_float((double)ts.tv_sec + (double)ts.tv_nsec/1e9);
}

HexaVal hexa_random(void) {
    return hexa_float(rand() / (double)RAND_MAX);
}

HexaVal hexa_char_code(HexaVal s, HexaVal idx);
// Bootstrap shim (same rationale as `join` above): SSOT modules use
// `char_code(s, i)` free-fn idiom which old hexa_v2 emits as
// `hexa_call2(char_code, ...)`. Shim binds the bare identifier to TAG_FN.
static HexaVal char_code;
HexaVal hexa_char_code(HexaVal s, HexaVal idx) {
    if (!HX_IS_STR(s)) return hexa_int(0);
    int i = HX_INT(idx);
    if (i < 0 || i >= (int)strlen(HX_STR(s))) return hexa_int(0);
    return hexa_int((unsigned char)HX_STR(s)[i]);
}

// `chr(n)` — Python/Ruby-style inverse of char_code: int byte → 1-char str.
// Used by void's sys_pty accumulator (chr(b) reassembles bytes from
// line_buf int values). Binds the free-fn idiom the way char_code does.
HexaVal hexa_from_char_code(HexaVal n);
static HexaVal chr;

// `bit_or(x, y)` — shim kept because `|` as binary op conflicts with lambda
// param delimiters `|x| x+1`. `&` and `^` are supported by parser directly.
HexaVal _hx_bit_or(HexaVal a, HexaVal b) {
    int64_t x = HX_IS_INT(a) ? HX_INT(a) : (int64_t)HX_FLOAT(a);
    int64_t y = HX_IS_INT(b) ? HX_INT(b) : (int64_t)HX_FLOAT(b);
    return hexa_int(x | y);
}
static HexaVal bit_or;

// ── Added: method-dispatch helpers (bt 34) ────────────────────
HexaVal hexa_str_parse_int(HexaVal s) {
    if (!HX_IS_STR(s)) return hexa_int(0);
    const char* cs = HX_STR(s);
    // hxa-20260423-013: throw on non-integer input so `try/catch` around
    // to_int("abc") (T24) and uncaught to_int("1.01") (T34) behave as
    // the language documents. Empty / whitespace-only input still
    // returns 0 — 812 callsites (env-var + optional-argv parsing)
    // depend on that defensive-fallback shape.
    const char* p = cs;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p == 0) return hexa_int(0);
    // P39 fix: auto-detect hex prefix (0x/0X) while preserving decimal
    // semantics for leading-zero inputs ("0777" -> 777, not octal 511).
    const char* digit_start = p;
    if (*digit_start == '+' || *digit_start == '-') digit_start++;
    int base = 10;
    if (digit_start[0] == '0' && (digit_start[1] == 'x' || digit_start[1] == 'X')) base = 16;
    char* endptr = NULL;
    long long v = strtoll(p, &endptr, base);
    if (endptr == p) {
        // no digits consumed — "abc", "--", "" after trim handled above
        char msg[256];
        snprintf(msg, sizeof(msg), "error: to_int: not an integer: \"%s\"", cs);
        hexa_throw(hexa_str(msg));
        return hexa_int(0);
    }
    // Reject trailing non-whitespace garbage ("1.01", "123abc", "5 six").
    while (*endptr == ' ' || *endptr == '\t' || *endptr == '\n' || *endptr == '\r') endptr++;
    if (*endptr != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "error: to_int: trailing garbage in \"%s\"", cs);
        hexa_throw(hexa_str(msg));
        return hexa_int(0);
    }
    return hexa_int((int64_t)v);
}

HexaVal hexa_str_parse_float(HexaVal s) {
    if (!HX_IS_STR(s)) return hexa_float(0.0);
    return hexa_float(strtod(HX_STR(s), NULL));
}

// M1 full · str_ext Step 5 (hxa-20260423-003): rt_str_trim_start/end —
// codegen emits rt_str_* directly; hexa_str_trim_start/end shims retired.
HexaVal rt_str_trim_start(HexaVal s) {
    if (!HX_IS_STR(s)) return s;
    char* p = HX_STR(s);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return hexa_str_own(strdup(p));
}

HexaVal rt_str_trim_end(HexaVal s) {
    if (!HX_IS_STR(s)) return s;
    int len = strlen(HX_STR(s));
    while (len > 0 && (HX_STR(s)[len-1] == ' ' || HX_STR(s)[len-1] == '\t' || HX_STR(s)[len-1] == '\n' || HX_STR(s)[len-1] == '\r')) len--;
    return hexa_str_own(strndup(HX_STR(s), len));
}

// Byte-based slice: [start, end) clamped to length
HexaVal hexa_str_slice(HexaVal s, HexaVal start, HexaVal end) {
    if (!HX_IS_STR(s)) return hexa_str("");
    int len = (int)strlen(HX_STR(s));
    int a = (int)HX_INT(start), b = (int)HX_INT(end);
    if (a < 0) a = 0;
    if (b > len) b = len;
    if (a > b) a = b;
    return hexa_str_own(strndup(HX_STR(s) + a, b - a));
}

HexaVal hexa_array_slice(HexaVal arr, HexaVal start, HexaVal end) {
    if (!HX_IS_ARRAY(arr)) return hexa_array_new();
    int n = HX_ARR_LEN(arr);
    int a = (int)HX_INT(start), b = (int)HX_INT(end);
    if (a < 0) a = 0;
    if (b > n) b = n;
    if (a > b) a = b;
    HexaVal out = hexa_array_new();
    for (int i = a; i < b; i++) out = hexa_array_push(out, HX_ARR_ITEMS(arr)[i]);
    return out;
}

HexaVal __hexa_range_array(HexaVal start, HexaVal end, int inclusive) {
    HexaVal out = hexa_array_new();
    int64_t s = HX_INT(start), e = HX_INT(end);
    if (inclusive) {
        for (int64_t i = s; i <= e; i++) out = hexa_array_push(out, hexa_int(i));
    } else {
        for (int64_t i = s; i < e; i++) out = hexa_array_push(out, hexa_int(i));
    }
    return out;
}

HexaVal hexa_array_map(HexaVal arr, HexaVal fn) {
    if (!HX_IS_ARRAY(arr)) return hexa_array_new();
    HexaVal out = hexa_array_new();
    for (int i = 0; i < HX_ARR_LEN(arr); i++) {
        out = hexa_array_push(out, hexa_call1(fn, HX_ARR_ITEMS(arr)[i]));
    }
    return out;
}

HexaVal hexa_array_filter(HexaVal arr, HexaVal fn) {
    if (!HX_IS_ARRAY(arr)) return hexa_array_new();
    HexaVal out = hexa_array_new();
    for (int i = 0; i < HX_ARR_LEN(arr); i++) {
        HexaVal keep = hexa_call1(fn, HX_ARR_ITEMS(arr)[i]);
        if (hexa_truthy(keep)) out = hexa_array_push(out, HX_ARR_ITEMS(arr)[i]);
    }
    return out;
}

HexaVal hexa_array_fold(HexaVal arr, HexaVal init, HexaVal fn) {
    if (!HX_IS_ARRAY(arr)) return init;
    HexaVal acc = init;
    for (int i = 0; i < HX_ARR_LEN(arr); i++) {
        acc = hexa_call2(fn, acc, HX_ARR_ITEMS(arr)[i]);
    }
    return acc;
}

HexaVal hexa_array_index_of(HexaVal arr, HexaVal item) {
    if (!HX_IS_ARRAY(arr)) return hexa_int(-1);
    for (int i = 0; i < HX_ARR_LEN(arr); i++) {
        if (hexa_truthy(hexa_eq(HX_ARR_ITEMS(arr)[i], item))) return hexa_int(i);
    }
    return hexa_int(-1);
}

// Higher-order array predicates + scans. Mirror interpreter semantics
// at self/hexa_full.hexa:15189+ using hexa_call1 for callback dispatch
// (same pattern as hexa_array_map/filter/fold).

HexaVal hexa_map_any(HexaVal m, HexaVal pred);
HexaVal hexa_map_all(HexaVal m, HexaVal pred);

HexaVal hexa_array_any(HexaVal arr, HexaVal fn) {
    // Map receiver: delegate to hexa_map_any (2-arg predicate over k,v).
    // Matches interpreter dispatch at self/hexa_full.hexa:15705-15718.
    if (HX_IS_MAP(arr)) return hexa_map_any(arr, fn);
    if (!HX_IS_ARRAY(arr)) return hexa_bool(0);
    for (int i = 0; i < HX_ARR_LEN(arr); i++) {
        if (hexa_truthy(hexa_call1(fn, HX_ARR_ITEMS(arr)[i]))) return hexa_bool(1);
    }
    return hexa_bool(0);
}

HexaVal hexa_array_all(HexaVal arr, HexaVal fn) {
    // Map receiver: delegate to hexa_map_all. Empty map → true (vacuous).
    // Matches interpreter dispatch at self/hexa_full.hexa:15719-15732.
    if (HX_IS_MAP(arr)) return hexa_map_all(arr, fn);
    if (!HX_IS_ARRAY(arr)) return hexa_bool(1);
    for (int i = 0; i < HX_ARR_LEN(arr); i++) {
        if (!hexa_truthy(hexa_call1(fn, HX_ARR_ITEMS(arr)[i]))) return hexa_bool(0);
    }
    return hexa_bool(1);
}

HexaVal hexa_array_count(HexaVal arr, HexaVal fn) {
    if (!HX_IS_ARRAY(arr)) return hexa_int(0);
    int64_t c = 0;
    for (int i = 0; i < HX_ARR_LEN(arr); i++) {
        if (hexa_truthy(hexa_call1(fn, HX_ARR_ITEMS(arr)[i]))) c++;
    }
    return hexa_int(c);
}

HexaVal hexa_map_count(HexaVal m, HexaVal pred);

// count_poly: dispatch `.count()` method by receiver type. String form
// takes a substring arg (hexa_str_count_substr); map form takes a 2-arg
// predicate fn(k,v) or acts as len() when pred is void; array form takes
// a 1-arg predicate fn (hexa_array_count). Matches interpreter at
// self/hexa_full.hexa:14997-15014 (str), 15689-15704 (map), 15351-15358 (arr).
HexaVal hexa_count_poly(HexaVal obj, HexaVal arg) {
    if (HX_IS_STR(obj) && HX_IS_STR(arg)) {
        return hexa_str_count_substr(obj, arg);
    }
    if (HX_IS_MAP(obj)) {
        return hexa_map_count(obj, arg);
    }
    return hexa_array_count(obj, arg);
}

// contains_poly: dispatch `.contains()` method by receiver type. String
// form is substring search; array form is element-eq search. Both return
// bool. Matches interpreter self/hexa_full.hexa:14945 (str) and
// 15186-15192 (arr).
HexaVal hexa_contains_poly(HexaVal obj, HexaVal arg) {
    if (HX_IS_STR(obj)) {
        return hexa_bool(hexa_str_contains(obj, arg));
    }
    return hexa_bool(hexa_array_contains(obj, arg));
}

// find: first element matching predicate; void if none.
HexaVal hexa_array_find(HexaVal arr, HexaVal fn) {
    if (!HX_IS_ARRAY(arr)) return hexa_void();
    for (int i = 0; i < HX_ARR_LEN(arr); i++) {
        HexaVal it = HX_ARR_ITEMS(arr)[i];
        if (hexa_truthy(hexa_call1(fn, it))) return it;
    }
    return hexa_void();
}

// Polymorphic `.find()` dispatch — .find() 이 string `.find(needle)` 과
// array `.find(pred)` 에서 의미/반환 타입이 달라 codegen 단일 사이트에서
// 정적 dispatch 불가 (이전엔 모든 경우 str_index_of 로 라우팅 → array
// 시 -1 반환). 런타임에서 receiver 타입 보고 양쪽에 위임.
HexaVal hexa_find_poly(HexaVal obj, HexaVal arg) {
    if (HX_IS_ARRAY(obj)) return hexa_array_find(obj, arg);
    return hexa_int(hexa_str_index_of(obj, arg));
}

// `0..5` / `1..=10` / `0..10 step 2` — evaluates a Range at expression
// position to an int array. 이전엔 ForStmt 경로에만 Range codegen 이 있어
// `let r = 0..5` 같은 일반 expr 사용 시 "CODEGEN ERROR: unhandled expr
// kind: Range" 발생. Interp 의 NK_RANGE 분기 (hexa_full.hexa:7425) 의
// AOT counterpart.
HexaVal hexa_range_array(HexaVal start, HexaVal end, HexaVal step, int inclusive) {
    HexaVal result = hexa_array_new();
    int64_t s = HX_INT(start);
    int64_t e = HX_INT(end);
    int64_t st = HX_IS_VOID(step) ? 1 : HX_INT(step);
    if (st <= 0) st = 1;
    if (inclusive) {
        for (int64_t i = s; i <= e; i += st) result = hexa_array_push(result, hexa_int(i));
    } else {
        for (int64_t i = s; i < e; i += st) result = hexa_array_push(result, hexa_int(i));
    }
    return result;
}

// flat_map: map then flatten one level. Non-array callback results
// are pushed as-is (matches interpreter fallback at hexa_full.hexa:15211).
HexaVal hexa_array_flat_map(HexaVal arr, HexaVal fn) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(arr)) return out;
    for (int i = 0; i < HX_ARR_LEN(arr); i++) {
        HexaVal sub = hexa_call1(fn, HX_ARR_ITEMS(arr)[i]);
        if (HX_IS_ARRAY(sub)) {
            for (int j = 0; j < HX_ARR_LEN(sub); j++) {
                out = hexa_array_push(out, HX_ARR_ITEMS(sub)[j]);
            }
        } else {
            out = hexa_array_push(out, sub);
        }
    }
    return out;
}

// enumerate: [[idx, item], ...] — matches hexa_full.hexa:15220-15228.
HexaVal hexa_array_enumerate(HexaVal arr) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(arr)) return out;
    for (int i = 0; i < HX_ARR_LEN(arr); i++) {
        HexaVal pair = hexa_array_new();
        pair = hexa_array_push(pair, hexa_int((int64_t)i));
        pair = hexa_array_push(pair, HX_ARR_ITEMS(arr)[i]);
        out = hexa_array_push(out, pair);
    }
    return out;
}

// is_empty: true if array/string has zero items/bytes. Map also uses
// map_len — handled in hexa_is_empty below (polymorphic).
HexaVal hexa_is_empty(HexaVal v) {
    if (HX_IS_ARRAY(v)) return hexa_bool(HX_ARR_LEN(v) == 0);
    if (HX_IS_STR(v)) {
        const char* s = HX_STR(v);
        return hexa_bool(!s || s[0] == '\0');
    }
    return hexa_bool(1); // null/undefined/etc. → empty
}

// min/max: reduction by element. Uses hexa_cmp_lt/gt which already handle
// int/float mixing. Empty array returns void (matches interpreter at
// hexa_full.hexa:15344).
HexaVal hexa_array_min(HexaVal arr) {
    if (!HX_IS_ARRAY(arr)) return hexa_void();
    int64_t n = HX_ARR_LEN(arr);
    if (n == 0) return hexa_void();
    HexaVal best = HX_ARR_ITEMS(arr)[0];
    for (int64_t i = 1; i < n; i++) {
        HexaVal it = HX_ARR_ITEMS(arr)[i];
        if (hexa_truthy(hexa_cmp_lt(it, best))) best = it;
    }
    return best;
}

HexaVal hexa_array_max(HexaVal arr) {
    if (!HX_IS_ARRAY(arr)) return hexa_void();
    int64_t n = HX_ARR_LEN(arr);
    if (n == 0) return hexa_void();
    HexaVal best = HX_ARR_ITEMS(arr)[0];
    for (int64_t i = 1; i < n; i++) {
        HexaVal it = HX_ARR_ITEMS(arr)[i];
        if (hexa_truthy(hexa_cmp_gt(it, best))) best = it;
    }
    return best;
}

// flatten: one-level; non-array elements passed through.
// Matches interpreter semantics at hexa_full.hexa:15316-15327.
HexaVal hexa_array_flatten(HexaVal arr) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(arr)) return out;
    for (int64_t i = 0; i < HX_ARR_LEN(arr); i++) {
        HexaVal it = HX_ARR_ITEMS(arr)[i];
        if (HX_IS_ARRAY(it)) {
            for (int64_t j = 0; j < HX_ARR_LEN(it); j++) {
                out = hexa_array_push(out, HX_ARR_ITEMS(it)[j]);
            }
        } else {
            out = hexa_array_push(out, it);
        }
    }
    return out;
}

// for_each: side-effect iteration. Returns void (hexa_full.hexa:15187).
HexaVal hexa_array_for_each(HexaVal arr, HexaVal fn) {
    if (!HX_IS_ARRAY(arr)) return hexa_void();
    for (int64_t i = 0; i < HX_ARR_LEN(arr); i++) {
        hexa_call1(fn, HX_ARR_ITEMS(arr)[i]);
    }
    return hexa_void();
}

// fill: new array of same length, every slot set to v.
// Matches interpreter: returns a NEW array rather than mutating in place
// (hexa_full.hexa:15486-15494).
HexaVal hexa_array_fill(HexaVal arr, HexaVal v) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(arr)) return out;
    int64_t n = HX_ARR_LEN(arr);
    for (int64_t i = 0; i < n; i++) {
        out = hexa_array_push(out, v);
    }
    return out;
}

// rt#32-O (r5-a10.5-A, 2026-04-20): leak-fix primitives for CLM training.
//
// Root cause: train_clm.hexa per-step has ~88 _tz(N) calls (decoder forward
// 12-block stack). Each _tz did N hexa_array_push() from cap=0, accumulating
// to a malloc'd buffer of N*sizeof(HexaVal) ~= N*32B that hexa never frees
// (no GC, value-typed). At seq=512 d=768 that is 12.6 MB per call, ~1.1 GB
// per step = 1.5 GB/min observed leak. Two new primitives fix this:
//
// 1) hexa_array_zeros_float(n): single-shot calloc of N HexaVal slots
//    pre-filled with hexa_float(0.0). Returns array with cap=len=n. No
//    realloc churn (was 17 grows per call at N=393K). Replaces _tz body.
//
// 2) hexa_array_free(arr): frees items buffer + descriptor. Hexa is
//    pass-by-value at HexaVal level, but the .arr_ptr / .items pointers
//    are shared. Caller MUST guarantee no other live alias. Used at
//    end-of-train_step to reclaim block_hs / fwd / bwd buffers known to
//    be local. Returns hexa_void().
HexaVal hexa_array_zeros_float(HexaVal nv) {
    if (_hx_stats_on()) _hx_stats_array_new++;
    HexaVal out = {.tag=TAG_ARRAY};
    HX_SET_ARR_PTR(out, (HexaArr*)calloc(1, sizeof(HexaArr)));
    int64_t n = HX_IS_INT(nv) ? HX_INT(nv) : (int64_t)__hx_to_double(nv);
    if (n <= 0) return out;
    HexaVal* items = (HexaVal*)malloc(sizeof(HexaVal) * (size_t)n);
    if (!items) { fprintf(stderr, "OOM in array_zeros_float n=%lld\n", (long long)n); exit(1); }
    HexaVal zero = {.tag=TAG_FLOAT, .f=0.0};
    for (int64_t i = 0; i < n; i++) items[i] = zero;
    HX_SET_ARR_ITEMS(out, items);
    HX_SET_ARR_LEN(out, (int)n);
    HX_SET_ARR_CAP(out, (int)n);  // positive → heap
    return out;
}

HexaVal hexa_array_free(HexaVal arr) {
    if (!HX_IS_ARRAY(arr)) return hexa_void();
    HexaArr* p = arr.arr_ptr;
    if (!p) return hexa_void();
    // Only free heap-backed buffers (cap>0). Arena-backed (cap<0) belong
    // to the arena slab and must not be passed to free().
    if (p->items && p->cap > 0) {
        free(p->items);
    }
    p->items = NULL;
    p->len = 0;
    p->cap = 0;
    free(p);
    return hexa_void();
}

// take(n): first n elements (or entire array if n ≥ len).
HexaVal hexa_array_take(HexaVal arr, HexaVal nv) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(arr)) return out;
    int64_t n = HX_IS_INT(nv) ? HX_INT(nv) : (int64_t)__hx_to_double(nv);
    int64_t len = HX_ARR_LEN(arr);
    int64_t k = n < len ? n : len;
    if (k < 0) k = 0;
    for (int64_t i = 0; i < k; i++) {
        out = hexa_array_push(out, HX_ARR_ITEMS(arr)[i]);
    }
    return out;
}

// drop(n): skip first n elements, return remainder.
HexaVal hexa_array_drop(HexaVal arr, HexaVal nv) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(arr)) return out;
    int64_t n = HX_IS_INT(nv) ? HX_INT(nv) : (int64_t)__hx_to_double(nv);
    int64_t len = HX_ARR_LEN(arr);
    int64_t start = n < 0 ? 0 : (n > len ? len : n);
    for (int64_t i = start; i < len; i++) {
        out = hexa_array_push(out, HX_ARR_ITEMS(arr)[i]);
    }
    return out;
}

// zip(other): pairs [a_i, b_i] up to min(len(a), len(b)).
// Matches interpreter at hexa_full.hexa:15258-15270.
HexaVal hexa_array_zip(HexaVal a, HexaVal b) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(a) || !HX_IS_ARRAY(b)) return out;
    int64_t na = HX_ARR_LEN(a), nb = HX_ARR_LEN(b);
    int64_t k = na < nb ? na : nb;
    for (int64_t i = 0; i < k; i++) {
        HexaVal pair = hexa_array_new();
        pair = hexa_array_push(pair, HX_ARR_ITEMS(a)[i]);
        pair = hexa_array_push(pair, HX_ARR_ITEMS(b)[i]);
        out = hexa_array_push(out, pair);
    }
    return out;
}

// chunk(n): split into non-overlapping chunks of size n (last may be shorter).
// Matches interpreter at hexa_full.hexa:15363-15378.
HexaVal hexa_array_chunk(HexaVal arr, HexaVal nv) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(arr)) return out;
    int64_t n = HX_IS_INT(nv) ? HX_INT(nv) : (int64_t)__hx_to_double(nv);
    if (n <= 0) return out;
    int64_t len = HX_ARR_LEN(arr);
    for (int64_t i = 0; i < len; i += n) {
        HexaVal chunk = hexa_array_new();
        int64_t stop = i + n;
        if (stop > len) stop = len;
        for (int64_t j = i; j < stop; j++) {
            chunk = hexa_array_push(chunk, HX_ARR_ITEMS(arr)[j]);
        }
        out = hexa_array_push(out, chunk);
    }
    return out;
}

// window(n): sliding windows of size n (step 1). Empty if n > len or n ≤ 0.
// Matches interpreter at hexa_full.hexa:15380-15395.
HexaVal hexa_array_window(HexaVal arr, HexaVal nv) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(arr)) return out;
    int64_t n = HX_IS_INT(nv) ? HX_INT(nv) : (int64_t)__hx_to_double(nv);
    int64_t len = HX_ARR_LEN(arr);
    if (n <= 0 || n > len) return out;
    for (int64_t i = 0; i + n <= len; i++) {
        HexaVal win = hexa_array_new();
        for (int64_t j = 0; j < n; j++) {
            win = hexa_array_push(win, HX_ARR_ITEMS(arr)[i + j]);
        }
        out = hexa_array_push(out, win);
    }
    return out;
}

// unique: dedupe by hexa_eq. O(n²) — matches interpreter's equality
// check (hexa_full.hexa:15263-15277). Scaling to hash-set is a follow-up.
HexaVal hexa_array_unique(HexaVal arr) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(arr)) return out;
    int64_t len = HX_ARR_LEN(arr);
    for (int64_t i = 0; i < len; i++) {
        HexaVal it = HX_ARR_ITEMS(arr)[i];
        int seen = 0;
        for (int64_t j = 0; j < HX_ARR_LEN(out); j++) {
            if (hexa_truthy(hexa_eq(HX_ARR_ITEMS(out)[j], it))) { seen = 1; break; }
        }
        if (!seen) out = hexa_array_push(out, it);
    }
    return out;
}

// rotate(k): k>0 shifts left (items[k] becomes new head); k<0 shifts right.
// k is normalized mod len — matches interpreter at hexa_full.hexa:15486-15499.
HexaVal hexa_array_rotate(HexaVal arr, HexaVal kv) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(arr)) return out;
    int64_t n = HX_ARR_LEN(arr);
    if (n == 0) return out;
    int64_t k = HX_IS_INT(kv) ? HX_INT(kv) : (int64_t)__hx_to_double(kv);
    k = k % n;
    if (k < 0) k += n;
    for (int64_t i = 0; i < n; i++) {
        out = hexa_array_push(out, HX_ARR_ITEMS(arr)[(i + k) % n]);
    }
    return out;
}

// partition(pred): returns [matching, non_matching] as a 2-element array.
// Matches interpreter at hexa_full.hexa:15437-15449.
HexaVal hexa_array_partition(HexaVal arr, HexaVal fn) {
    HexaVal matching = hexa_array_new();
    HexaVal rest = hexa_array_new();
    if (HX_IS_ARRAY(arr)) {
        for (int64_t i = 0; i < HX_ARR_LEN(arr); i++) {
            HexaVal it = HX_ARR_ITEMS(arr)[i];
            if (hexa_truthy(hexa_call1(fn, it))) {
                matching = hexa_array_push(matching, it);
            } else {
                rest = hexa_array_push(rest, it);
            }
        }
    }
    HexaVal out = hexa_array_new();
    out = hexa_array_push(out, matching);
    out = hexa_array_push(out, rest);
    return out;
}

// interleave(other): alternates items from both arrays up to max length.
// When one array runs out, the other's remaining items still alternate in.
// Matches interpreter at hexa_full.hexa:15451-15464.
HexaVal hexa_array_interleave(HexaVal a, HexaVal b) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(a)) {
        return HX_IS_ARRAY(b) ? b : out;
    }
    if (!HX_IS_ARRAY(b)) return a;
    int64_t na = HX_ARR_LEN(a), nb = HX_ARR_LEN(b);
    int64_t m = na > nb ? na : nb;
    for (int64_t i = 0; i < m; i++) {
        if (i < na) out = hexa_array_push(out, HX_ARR_ITEMS(a)[i]);
        if (i < nb) out = hexa_array_push(out, HX_ARR_ITEMS(b)[i]);
    }
    return out;
}

// scan(init, fn): like fold but returns all intermediate accumulators.
// Result length is len(arr) + 1 (includes init as first element).
// Matches interpreter at hexa_full.hexa:15426-15435.
HexaVal hexa_array_scan(HexaVal arr, HexaVal init, HexaVal fn) {
    HexaVal out = hexa_array_new();
    out = hexa_array_push(out, init);
    if (!HX_IS_ARRAY(arr)) return out;
    HexaVal acc = init;
    for (int64_t i = 0; i < HX_ARR_LEN(arr); i++) {
        acc = hexa_call2(fn, acc, HX_ARR_ITEMS(arr)[i]);
        out = hexa_array_push(out, acc);
    }
    return out;
}

// product(a): multiplicative reduce. Mirrors hexa_sum but with mult.
// Empty array returns int 1 (multiplicative identity).
HexaVal hexa_array_product(HexaVal arr) {
    if (!HX_IS_ARRAY(arr)) return hexa_int(1);
    int64_t n = HX_ARR_LEN(arr);
    int has_float = 0;
    int64_t int_total = 1;
    double float_total = 1.0;
    for (int64_t i = 0; i < n; i++) {
        HexaVal e = HX_ARR_ITEMS(arr)[i];
        if (HX_IS_FLOAT(e)) { has_float = 1; float_total *= HX_FLOAT(e); }
        else { int_total *= HX_INT(e); }
    }
    if (has_float) return hexa_float((double)int_total * float_total);
    return hexa_int(int_total);
}

// mean(a): float average. Empty array returns float 0.0.
HexaVal hexa_array_mean(HexaVal arr) {
    if (!HX_IS_ARRAY(arr)) return hexa_float(0.0);
    int64_t n = HX_ARR_LEN(arr);
    if (n == 0) return hexa_float(0.0);
    double total = 0.0;
    for (int64_t i = 0; i < n; i++) {
        total += __hx_to_double(HX_ARR_ITEMS(arr)[i]);
    }
    return hexa_float(total / (double)n);
}

// swap(i, j): returns new array with items at i and j swapped.
// Out-of-range indices return original array copy.
// Matches interpreter at hexa_full.hexa:15497-15512.
HexaVal hexa_array_swap(HexaVal arr, HexaVal iv, HexaVal jv) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(arr)) return out;
    int64_t n = HX_ARR_LEN(arr);
    int64_t i = HX_IS_INT(iv) ? HX_INT(iv) : (int64_t)__hx_to_double(iv);
    int64_t j = HX_IS_INT(jv) ? HX_INT(jv) : (int64_t)__hx_to_double(jv);
    if (i < 0 || j < 0 || i >= n || j >= n) {
        for (int64_t k = 0; k < n; k++) out = hexa_array_push(out, HX_ARR_ITEMS(arr)[k]);
        return out;
    }
    for (int64_t k = 0; k < n; k++) {
        if (k == i) out = hexa_array_push(out, HX_ARR_ITEMS(arr)[j]);
        else if (k == j) out = hexa_array_push(out, HX_ARR_ITEMS(arr)[i]);
        else out = hexa_array_push(out, HX_ARR_ITEMS(arr)[k]);
    }
    return out;
}

// group_by: apply fn(item) → key, bucket items into map[str(key)] = [items].
// Interpreter (self/hexa_full.hexa:15323-15344) compares keys via
// val_to_string, so we do the same via hexa_to_string for parity — non-
// string keys still group correctly. Values accumulate as arrays.
HexaVal hexa_array_group_by(HexaVal arr, HexaVal fn) {
    HexaVal out = hexa_map_new();
    if (!HX_IS_ARRAY(arr)) return out;
    for (int i = 0; i < HX_ARR_LEN(arr); i++) {
        HexaVal item = HX_ARR_ITEMS(arr)[i];
        HexaVal key = hexa_call1(fn, item);
        const char* k_str = hexa_str_as_ptr(hexa_to_string(key));
        if (!k_str) continue;
        HexaVal bucket;
        if (hexa_map_contains_key(out, k_str)) {
            bucket = hexa_map_get(out, k_str);
        } else {
            bucket = hexa_array_new();
        }
        bucket = hexa_array_push(bucket, item);
        out = hexa_map_set(out, k_str, bucket);
    }
    return out;
}

// frequencies: count occurrences per value; keys are stringified via
// hexa_to_string (same as group_by). Returns map<str, int>. Matches
// interpreter at self/hexa_full.hexa:15509-15528.
HexaVal hexa_array_frequencies(HexaVal arr) {
    HexaVal out = hexa_map_new();
    if (!HX_IS_ARRAY(arr)) return out;
    for (int i = 0; i < HX_ARR_LEN(arr); i++) {
        const char* k = hexa_str_as_ptr(hexa_to_string(HX_ARR_ITEMS(arr)[i]));
        if (!k) continue;
        int64_t cur = 0;
        if (hexa_map_contains_key(out, k)) {
            HexaVal v = hexa_map_get(out, k);
            cur = HX_IS_INT(v) ? HX_INT(v) : (int64_t)__hx_to_double(v);
        }
        out = hexa_map_set(out, k, hexa_int(cur + 1));
    }
    return out;
}

// sample(n): return n items drawn uniformly at random (with replacement)
// from arr. Empty array or n<=0 returns empty array. Uses rand()/RAND_MAX
// like hexa_random() so the RNG stream is shared. Matches interpreter at
// self/hexa_full.hexa:15544-15556.
HexaVal hexa_array_sample(HexaVal arr, HexaVal nv) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(arr)) return out;
    int64_t n_items = HX_ARR_LEN(arr);
    int64_t n = HX_IS_INT(nv) ? HX_INT(nv) : (int64_t)__hx_to_double(nv);
    if (n_items == 0 || n <= 0) return out;
    for (int64_t i = 0; i < n; i++) {
        int64_t idx = (int64_t)(rand() / (double)RAND_MAX * (double)n_items);
        if (idx >= n_items) idx = n_items - 1;
        out = hexa_array_push(out, HX_ARR_ITEMS(arr)[idx]);
    }
    return out;
}

// substr: JS-style substring(start, length). length defaults to "rest of
// string" when not supplied. Negative start clamps to 0, negative length
// to 0, end clamps to strlen. Matches interpreter at
// self/hexa_full.hexa:14959-14972.
HexaVal hexa_str_substr(HexaVal s, HexaVal start_v, HexaVal len_v) {
    if (!HX_IS_STR(s)) return hexa_str("");
    int64_t slen = (int64_t)strlen(HX_STR(s));
    int64_t start = HX_IS_INT(start_v) ? HX_INT(start_v) : (int64_t)__hx_to_double(start_v);
    if (start < 0) start = 0;
    if (start > slen) start = slen;
    int64_t count;
    if (HX_TAG(len_v) == TAG_VOID) {
        count = slen - start;
    } else {
        count = HX_IS_INT(len_v) ? HX_INT(len_v) : (int64_t)__hx_to_double(len_v);
    }
    if (count < 0) count = 0;
    int64_t end_idx = start + count;
    if (end_idx > slen) end_idx = slen;
    return hexa_str_substring(s, hexa_int(start), hexa_int(end_idx));
}

HexaVal hexa_str_bytes(HexaVal s) {
    if (!HX_IS_STR(s)) return hexa_array_new();
    HexaVal out = hexa_array_new();
    for (const unsigned char* p = (const unsigned char*)HX_STR(s); *p; p++) {
        out = hexa_array_push(out, hexa_int((int64_t)*p));
    }
    return out;
}

// ── bt-71: libc / builtin family wrappers ────────────────────────────
// interpreter.hexa references bare idents like `exit(code)`, `tanh(x)`,
// `input()`, `is_error(v)`, `read_lines(p)`, `env_var(n)`, `hex(n)`, etc.
// Codegen used to emit `hexa_call1(exit, hexa_int(0))` which passes the
// libc function pointer (wrong type) to hexa_call1(HexaVal, HexaVal).
// We now wrap each in a HexaVal-returning stub so codegen can emit a
// direct call with correct types.
#include <unistd.h>
#include <time.h>

HexaVal hexa_exit(HexaVal code) {
    int c = HX_IS_INT(code) ? (int)HX_INT(code)
          : HX_IS_FLOAT(code) ? (int)HX_FLOAT(code)
          : 0;
    fflush(stdout); fflush(stderr);
    exit(c);
    return hexa_void(); // unreachable
}

HexaVal hexa_sleep(HexaVal sec) {
    double s = HX_IS_FLOAT(sec) ? HX_FLOAT(sec)
             : HX_IS_INT(sec)   ? (double)HX_INT(sec)
             : 0.0;
    if (s <= 0.0) return hexa_void();
    struct timespec ts;
    ts.tv_sec  = (time_t)s;
    ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
    return hexa_void();
}

// Math family — accept int or float, return float
static double _hexa_f(HexaVal v) {
    if (HX_IS_FLOAT(v)) return HX_FLOAT(v);
    if (HX_IS_INT(v))   return (double)HX_INT(v);
    if (HX_IS_BOOL(v))  return HX_BOOL(v) ? 1.0 : 0.0;
    return 0.0;
}

// Safe float→int64 cast. C cast of NaN/Inf/out-of-range double is UB
// (typically INT64_MIN on x86). Returns 0 for NaN/Inf, clamps to int64
// range otherwise. Used by to_int() builtin.
int64_t hexa_float_to_int(double f) {
    if (isnan(f) || isinf(f)) return 0;
    if (f >= 9.2233720368547758e+18) return (int64_t)0x7fffffffffffffffLL;
    if (f <= -9.2233720368547758e+18) return (int64_t)0x8000000000000000LL;
    return (int64_t)f;
}

HexaVal hexa_math_tanh(HexaVal x) { return hexa_float(tanh(_hexa_f(x))); }
HexaVal hexa_math_sin(HexaVal x)  { return hexa_float(sin(_hexa_f(x))); }
HexaVal hexa_math_cos(HexaVal x)  { return hexa_float(cos(_hexa_f(x))); }
HexaVal hexa_math_tan(HexaVal x)  { return hexa_float(tan(_hexa_f(x))); }
HexaVal hexa_math_asin(HexaVal x) { return hexa_float(asin(_hexa_f(x))); }
HexaVal hexa_math_acos(HexaVal x) { return hexa_float(acos(_hexa_f(x))); }
HexaVal hexa_math_atan(HexaVal x) { return hexa_float(atan(_hexa_f(x))); }
HexaVal hexa_math_atan2(HexaVal y, HexaVal x) { return hexa_float(atan2(_hexa_f(y), _hexa_f(x))); }
HexaVal hexa_math_log(HexaVal x)  { return hexa_float(log(_hexa_f(x))); }
HexaVal hexa_math_exp(HexaVal x)  { return hexa_float(exp(_hexa_f(x))); }
HexaVal hexa_math_abs(HexaVal x)  { return hexa_float(fabs(_hexa_f(x))); }
HexaVal hexa_math_sqrt(HexaVal x) { return hexa_float(sqrt(_hexa_f(x))); }
HexaVal hexa_math_floor(HexaVal x){ return hexa_float(floor(_hexa_f(x))); }
HexaVal hexa_math_ceil(HexaVal x) { return hexa_float(ceil(_hexa_f(x))); }
HexaVal hexa_math_round(HexaVal x){ return hexa_float(round(_hexa_f(x))); }
HexaVal hexa_math_pow(HexaVal b, HexaVal e) { return hexa_float(pow(_hexa_f(b), _hexa_f(e))); }
HexaVal hexa_math_min(HexaVal a, HexaVal b) { return hexa_float(fmin(_hexa_f(a), _hexa_f(b))); }
HexaVal hexa_math_max(HexaVal a, HexaVal b) { return hexa_float(fmax(_hexa_f(a), _hexa_f(b))); }

// ── ML builtins: matvec, dot ─────────────────────────────────
HexaVal hexa_matvec(HexaVal w, HexaVal x, HexaVal rows_v, HexaVal cols_v) {
    int64_t rows = HX_IS_INT(rows_v) ? HX_INT(rows_v) : 0;
    int64_t cols = HX_IS_INT(cols_v) ? HX_INT(cols_v) : 0;
    if (rows <= 0 || cols <= 0) return hexa_array_new();
    HexaVal out = hexa_array_new();
    for (int64_t r = 0; r < rows; r++) {
        double acc = 0.0;
        for (int64_t c = 0; c < cols; c++) {
            HexaVal wv = hexa_index_get(w, hexa_int(r * cols + c));
            HexaVal xv = hexa_index_get(x, hexa_int(c));
            acc += _hexa_f(wv) * _hexa_f(xv);
        }
        out = hexa_array_push(out, hexa_float(acc));
    }
    return out;
}

HexaVal hexa_input(HexaVal prompt) {
    if (HX_IS_STR(prompt) && HX_STR(prompt) && HX_STR(prompt)[0]) {
        fputs(HX_STR(prompt), stdout);
        fflush(stdout);
    }
    char* buf = NULL;
    size_t cap = 0;
    ssize_t n = getline(&buf, &cap, stdin);
    if (n < 0) {
        if (buf) free(buf);
        return hexa_str("");
    }
    // strip trailing \n
    if (n > 0 && buf[n-1] == '\n') buf[n-1] = 0;
    return hexa_str_own(buf);
}

HexaVal hexa_is_error(HexaVal v) {
    // No TAG_ERROR in runtime; interpreter uses ad-hoc Val(TAG_ERROR,...)
    // convention (TAG_ERROR name not defined in runtime tags). Treat as
    // "false" here unless TAG_STR and starts with "ERR:" sentinel.
    if (HX_IS_STR(v) && HX_STR(v) && strncmp(HX_STR(v), "ERR:", 4) == 0) return hexa_bool(1);
    return hexa_bool(0);
}

HexaVal rt_read_lines(HexaVal path) {
    HexaVal content = rt_read_file(path);
    if (!HX_IS_STR(content)) return hexa_array_new();
    HexaVal out = hexa_array_new();
    const char* p = HX_STR(content);
    const char* start = p;
    while (*p) {
        if (*p == '\n') {
            size_t len = (size_t)(p - start);
            char* line = (char*)malloc(len + 1);
            memcpy(line, start, len);
            line[len] = 0;
            out = hexa_array_push(out, hexa_str_own(line));
            start = p + 1;
        }
        p++;
    }
    if (p > start) {
        size_t len = (size_t)(p - start);
        char* line = (char*)malloc(len + 1);
        memcpy(line, start, len);
        line[len] = 0;
        out = hexa_array_push(out, hexa_str_own(line));
    }
    return out;
}

HexaVal hexa_from_char_code(HexaVal n) {
    int64_t code = HX_IS_INT(n) ? HX_INT(n) : (int64_t)_hexa_f(n);
    if (code < 0) code = 0;
    if (code < 0x80) {
        char* s = (char*)malloc(2); s[0] = (char)code; s[1] = 0;
        return hexa_str_own(s);
    }
    // UTF-8 encode
    char buf[5] = {0};
    if (code < 0x800) {
        buf[0] = (char)(0xC0 | (code >> 6));
        buf[1] = (char)(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        buf[0] = (char)(0xE0 | (code >> 12));
        buf[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (code & 0x3F));
    } else {
        buf[0] = (char)(0xF0 | (code >> 18));
        buf[1] = (char)(0x80 | ((code >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (code & 0x3F));
    }
    char* out = strdup(buf);
    return hexa_str_own(out);
}

HexaVal hexa_env_var(HexaVal name) {
    if (!HX_IS_STR(name) || !HX_STR(name)) return hexa_str("");
    // rt 32-L: side-channel for Val arena scope ops. Hexa-side env_push_scope /
    // env_pop_scope / call_user_fn invoke env("__HEXA_ARENA_*") to drive the C
    // arena without needing a transpiler-level builtin. Returns "0" / "1" so
    // existing callers that ignore the result are unaffected.
    if (HX_STR(name)[0] == '_' && HX_STR(name)[1] == '_' && HX_STR(name)[2] == 'H' &&
        strncmp(HX_STR(name), "__HEXA_ARENA_", 13) == 0) {
        const char* op = HX_STR(name) + 13;
        if (strcmp(op, "PUSH__") == 0) {
            hexa_val_arena_scope_push();
            return hexa_str("1");
        }
        if (strcmp(op, "POP__") == 0) {
            hexa_val_arena_scope_pop();
            return hexa_str("1");
        }
        if (strcmp(op, "HEAPIFY_RETURN__") == 0) {
            hexa_val_arena_heapify_return();
            return hexa_str("1");
        }
        if (strcmp(op, "ENABLED__") == 0) {
            return hexa_str(hexa_val_arena_on() ? "1" : "0");
        }
        if (strcmp(op, "STATS__") == 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "marks=%d", __hexa_val_mark_top);
            return hexa_str(buf);
        }
        // Unknown __HEXA_ARENA_* op — fall through to real getenv (returns "").
    }
    const char* v = getenv(HX_STR(name));
    return hexa_str(v ? v : "");
}

// setenv(name, value): POSIX setenv wrapper with overwrite=1. Empty / non-STR
// name is a no-op. Returns the stored value on success, "" on failure — so
// callers can treat it like env() in a set-then-read idiom without a second
// getenv round-trip. anima/serve_alm-style bash bridges previously used
// `env BAR=baz hexa run …` because the language had no setter; this closes
// the gap so .hexa contracts can configure child env directly before exec.
HexaVal hexa_setenv(HexaVal name, HexaVal value) {
    if (!HX_IS_STR(name) || !HX_STR(name) || HX_STR(name)[0] == '\0') return hexa_str("");
    const char* v = (HX_IS_STR(value) && HX_STR(value)) ? HX_STR(value) : "";
    if (setenv(HX_STR(name), v, 1) != 0) return hexa_str("");
    return hexa_str(v);
}

// exec_capture(cmd): spawn `/bin/sh -c cmd` and return
// [stdout_str, stderr_str, exit_code] with stderr split from stdout. Uses
// pipe/fork/execvp (not popen, which merges 2>&1 when you redirect it).
// Closes the anima bash-bridge pattern of `tool/serve_alm_persona.bash`
// (333 lines) that existed solely to separate stderr capture.
HexaVal hexa_exec_capture(HexaVal cmd) {
    HexaVal arr = hexa_array_new();
    if (!HX_IS_STR(cmd) || !HX_STR(cmd)) {
        arr = hexa_array_push(arr, hexa_str(""));
        arr = hexa_array_push(arr, hexa_str(""));
        arr = hexa_array_push(arr, hexa_int(127));
        return arr;
    }
    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) != 0) {
        arr = hexa_array_push(arr, hexa_str(""));
        arr = hexa_array_push(arr, hexa_str("pipe: stdout"));
        arr = hexa_array_push(arr, hexa_int(127));
        return arr;
    }
    if (pipe(err_pipe) != 0) {
        close(out_pipe[0]); close(out_pipe[1]);
        arr = hexa_array_push(arr, hexa_str(""));
        arr = hexa_array_push(arr, hexa_str("pipe: stderr"));
        arr = hexa_array_push(arr, hexa_int(127));
        return arr;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        arr = hexa_array_push(arr, hexa_str(""));
        arr = hexa_array_push(arr, hexa_str("fork failed"));
        arr = hexa_array_push(arr, hexa_int(127));
        return arr;
    }
    if (pid == 0) {
        // child: rewire stdout/stderr then exec shell
        dup2(out_pipe[1], 1);
        dup2(err_pipe[1], 2);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        execl("/bin/sh", "sh", "-c", HX_STR(cmd), (char*)NULL);
        _exit(127);
    }
    // parent: close write ends, drain both pipes.
    // Simple drain loop: alternate non-blocking would be better but for
    // typical build-script command sizes (sub-MB) sequential is fine —
    // we close(write) immediately so EOF propagates once child exits.
    close(out_pipe[1]);
    close(err_pipe[1]);
    char buf[4096];
    size_t ocap = 4096, olen = 0;
    char* obuf = (char*)malloc(ocap);
    if (obuf) obuf[0] = '\0';
    size_t ecap = 4096, elen = 0;
    char* ebuf = (char*)malloc(ecap);
    if (ebuf) ebuf[0] = '\0';
    int of = out_pipe[0], ef = err_pipe[0];
    int open_mask = 3; // bit 0 = stdout, bit 1 = stderr
    while (open_mask) {
        if (open_mask & 1) {
            ssize_t n = read(of, buf, sizeof(buf));
            if (n > 0 && obuf) {
                while (olen + (size_t)n + 1 > ocap) {
                    ocap *= 2;
                    char* nb = (char*)realloc(obuf, ocap);
                    if (!nb) { free(obuf); obuf = NULL; break; }
                    obuf = nb;
                }
                if (obuf) { memcpy(obuf + olen, buf, (size_t)n); olen += (size_t)n; obuf[olen] = '\0'; }
            } else if (n == 0 || (n < 0)) {
                open_mask &= ~1;
            }
        }
        if (open_mask & 2) {
            ssize_t n = read(ef, buf, sizeof(buf));
            if (n > 0 && ebuf) {
                while (elen + (size_t)n + 1 > ecap) {
                    ecap *= 2;
                    char* nb = (char*)realloc(ebuf, ecap);
                    if (!nb) { free(ebuf); ebuf = NULL; break; }
                    ebuf = nb;
                }
                if (ebuf) { memcpy(ebuf + elen, buf, (size_t)n); elen += (size_t)n; ebuf[elen] = '\0'; }
            } else if (n == 0 || (n < 0)) {
                open_mask &= ~2;
            }
        }
    }
    close(of); close(ef);
    int status = 0;
    waitpid(pid, &status, 0);
    int exit_code;
    if (WIFEXITED(status))         exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))  exit_code = 128 + WTERMSIG(status);
    else                           exit_code = -1;
    arr = hexa_array_push(arr, obuf ? hexa_str_own(obuf) : hexa_str(""));
    arr = hexa_array_push(arr, ebuf ? hexa_str_own(ebuf) : hexa_str(""));
    arr = hexa_array_push(arr, hexa_int(exit_code));
    return arr;
}

HexaVal rt_delete_file(HexaVal path) {
    if (!HX_IS_STR(path) || !HX_STR(path)) return hexa_void();
    (void)unlink(HX_STR(path));
    return hexa_void();
}

HexaVal rt_append_file(HexaVal path, HexaVal content) {
    if (!HX_IS_STR(path) || !HX_STR(path)) return hexa_void();
    const char* data = (HX_IS_STR(content) && HX_STR(content)) ? HX_STR(content) : "";
    FILE* f = fopen(HX_STR(path), "ab");
    if (!f) return hexa_void();
    fwrite(data, 1, strlen(data), f);
    fclose(f);
    return hexa_void();
}

HexaVal hexa_bin(HexaVal n) {
    uint64_t v = HX_IS_INT(n) ? (uint64_t)HX_INT(n) : (uint64_t)_hexa_f(n);
    char buf[65]; int pos = 0;
    if (v == 0) { buf[pos++] = '0'; }
    while (v > 0 && pos < 64) { buf[pos++] = (char)('0' + (v & 1)); v >>= 1; }
    char* out = (char*)malloc(pos + 1);
    for (int i = 0; i < pos; i++) out[i] = buf[pos - 1 - i];
    out[pos] = 0;
    return hexa_str_own(out);
}

HexaVal hexa_hex(HexaVal n) {
    uint64_t v = HX_IS_INT(n) ? (uint64_t)HX_INT(n) : (uint64_t)_hexa_f(n);
    char* out = (char*)malloc(20);
    snprintf(out, 20, "%llx", (unsigned long long)v);
    return hexa_str_own(out);
}

// Reproducible-emit pin. When SOURCE_DATE_EPOCH is set (GNU/Debian
// convention, unix-seconds int) or HEXA_REPRODUCIBLE=1, wall-clock
// primitives return a fixed value so emitted artifacts (codegen output,
// manifests with embedded `now()`, chain-hash folds) are byte-identical
// across runs. Returns -1 when no pin is active.
// HEXA_REPRODUCIBLE=1 alone pins to 0 (epoch); SOURCE_DATE_EPOCH wins.
static int64_t hexa_pinned_epoch(void) {
    const char* sde = getenv("SOURCE_DATE_EPOCH");
    if (sde && *sde) {
        // strtoll tolerates leading whitespace and stops at first non-digit
        char* endp = NULL;
        long long v = strtoll(sde, &endp, 10);
        if (endp != sde && v >= 0) return (int64_t)v;
    }
    const char* repro = getenv("HEXA_REPRODUCIBLE");
    if (repro && repro[0] == '1' && repro[1] == '\0') return 0;
    return -1;
}

HexaVal hexa_timestamp(void) {
    int64_t pin = hexa_pinned_epoch();
    if (pin >= 0) return hexa_int(pin);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return hexa_int((int64_t)ts.tv_sec);
}

// FIX-A (Anima serving unblock, 2026-04-18) ─────────────────────────
// time_ms(): monotonic wall-clock milliseconds. Used by checkpoint.hexa /
// train_logger.hexa / eval_harness.hexa / train_7b_integrated.hexa for
// elapsed-time measurement (tok/s, save/load latency, per-step timing).
HexaVal hexa_time_ms(void) {
    int64_t pin = hexa_pinned_epoch();
    if (pin >= 0) return hexa_int(pin * 1000);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t ms = (int64_t)ts.tv_sec * 1000
               + (int64_t)(ts.tv_nsec / 1000000);
    return hexa_int(ms);
}

// byte_len(v): length in bytes of a string, array, or map. Unlike hexa_len
// (which rejects non-container tags), this is permissive — returns 0 on
// anything without a measurable length. checkpoint.hexa uses it on byte
// arrays returned by read_file_bytes.
HexaVal hexa_byte_len(HexaVal v) {
    if (HX_IS_STR(v))   return hexa_int(HX_STR(v) ? (int64_t)strlen(HX_STR(v)) : 0);
    if (HX_IS_ARRAY(v)) return hexa_int((int64_t)HX_ARR_LEN(v));
    if (HX_IS_MAP(v)) {
        HexaMapTable* t = HX_MAP_TBL(v);
        return hexa_int(t ? (int64_t)t->len : 0);
    }
    return hexa_int(0);
}

// dict_keys(m): return TAG_ARRAY of TAG_STR keys preserving insertion order.
// Thin alias over hexa_map_keys (which is already the canonical C symbol for
// HX_MAP key iteration). Kept as a distinct builtin so .hexa sources can
// use the more familiar `dict_keys(d)` spelling (tokenizer_bpe.hexa etc.).
HexaVal hexa_dict_keys(HexaVal m) {
    return hexa_map_keys(m);
}

// FIX-A (Anima stdlib unblock, 2026-04-19) ─────────────────────────
// 10 builtins required across ~230 Anima/serve/train .hexa files
// (read_stdin ×53, json_parse ×119, json_stringify ×37, json_encode ×5,
// json_decode ×4, http_get ×6, sleep_s ×2, to_bool/now_monotonic_s/
// utc_iso_now/utc_compact_now ×1-2). Keep surface minimal — no libcurl,
// no locale tables, no float parse-path perfection: goal is unblock
// compilation + serve_alm dogfooding.

// read_stdin(): slurp all of stdin into a TAG_STR. Returns "" on EOF-only.
// Used by cli tools + test harnesses that pipe input.
HexaVal hexa_read_stdin(void) {
    size_t cap = 4096, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) return hexa_str("");
    int c;
    while ((c = fgetc(stdin)) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) { free(buf); return hexa_str(""); }
            buf = nb;
        }
        buf[len++] = (char)c;
    }
    buf[len] = 0;
    return hexa_str_own(buf);
}

// sleep_s(n): sleep n seconds (int or float). Wraps nanosleep for
// sub-second precision. Returns void.
HexaVal hexa_sleep_s(HexaVal n) {
    double s = 0.0;
    if (HX_IS_INT(n))        s = (double)HX_INT(n);
    else if (HX_IS_FLOAT(n)) s = HX_FLOAT(n);
    else                     s = (double)hexa_as_num(n);
    if (s < 0) s = 0;
    struct timespec ts;
    ts.tv_sec  = (time_t)s;
    ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
    return hexa_void();
}

// now_monotonic_s(): TAG_FLOAT seconds from CLOCK_MONOTONIC (wall-clock
// elapsed, immune to system clock adjustment). Paired with time_ms()
// for finer-grained benchmarks.
HexaVal hexa_now_monotonic_s(void) {
    int64_t pin = hexa_pinned_epoch();
    if (pin >= 0) return hexa_float((double)pin);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return hexa_float((double)ts.tv_sec + (double)ts.tv_nsec / 1e9);
}

// mono_ns(): TAG_INT nanoseconds from CLOCK_MONOTONIC. Exposed because
// to_string(clock()) truncates sub-second precision via double→string
// formatting, which caused stderr_tmp collisions (fix 1472b62d). Callers
// building unique filenames should use to_string(mono_ns()) directly
// and skip the mktemp fork in runtime_tmpname fallback (perf).
HexaVal hexa_mono_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return hexa_int((int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec);
}

// utc_iso_now(): RFC-3339 / ISO-8601 UTC "YYYY-MM-DDTHH:MM:SSZ".
HexaVal hexa_utc_iso_now(void) {
    int64_t pin = hexa_pinned_epoch();
    time_t t = (pin >= 0) ? (time_t)pin : time(NULL);
    struct tm g;
    gmtime_r(&t, &g);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &g);
    return hexa_str(buf);
}

// utc_compact_now(): compact "YYYYMMDDHHMMSS" UTC (checkpoint filename).
HexaVal hexa_utc_compact_now(void) {
    int64_t pin = hexa_pinned_epoch();
    time_t t = (pin >= 0) ? (time_t)pin : time(NULL);
    struct tm g;
    gmtime_r(&t, &g);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &g);
    return hexa_str(buf);
}

// to_bool(v): generic truthy coercion. Mirrors hexa_truthy semantics
// but returns a TAG_BOOL HexaVal (as opposed to int).
HexaVal hexa_to_bool(HexaVal v) {
    return hexa_bool(hexa_truthy(v) ? 1 : 0);
}

// http_get(url): popen("curl -s <url>") → TAG_STR body. Keeps the
// dependency surface at /usr/bin/curl (universal on macOS/linux) and
// avoids pulling libcurl into runtime.c. Returns "" on curl-missing or
// non-zero exit. http_get is used by anima serve_alm health-checks +
// a handful of blowup-engine scrapers (~6 sites).
HexaVal hexa_http_get(HexaVal url) {
    if (!HX_IS_STR(url) || !HX_STR(url)) return hexa_str("");
    const char* u = HX_STR(url);
    // Reject URLs with shell-meta chars — we pass through /bin/sh via popen.
    for (const char* p = u; *p; p++) {
        char c = *p;
        if (c == '`' || c == '$' || c == ';' || c == '|' || c == '&'
            || c == '<' || c == '>' || c == '\n' || c == '\r'
            || c == '\'' || c == '"' || c == '\\') {
            return hexa_str("");
        }
    }
    char cmd[4096];
    int k = snprintf(cmd, sizeof(cmd), "curl -fsSL --max-time 30 '%s' 2>/dev/null", u);
    if (k < 0 || (size_t)k >= sizeof(cmd)) return hexa_str("");
    FILE* fp = popen(cmd, "r");
    if (!fp) return hexa_str("");
    size_t cap = 4096, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) { pclose(fp); return hexa_str(""); }
    size_t r;
    char chunk[2048];
    while ((r = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        if (len + r + 1 >= cap) {
            while (len + r + 1 >= cap) cap *= 2;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) { free(buf); pclose(fp); return hexa_str(""); }
            buf = nb;
        }
        memcpy(buf + len, chunk, r);
        len += r;
    }
    buf[len] = 0;
    pclose(fp);
    return hexa_str_own(buf);
}

// ── JSON mini (parse + stringify) ─────────────────────────────────
// Minimal recursive-descent JSON handler that covers Anima's use
// (tokenizer configs, checkpoint metadata, serve_alm request bodies).
// Supported: null / bool / int / float / string / array / object.
// Limits: no \uXXXX surrogates (passthrough as raw bytes), no +NaN/Inf.
// TAG mapping:
//   null   → TAG_VOID  (hexa_void)
//   true   → TAG_BOOL 1
//   false  → TAG_BOOL 0
//   int    → TAG_INT
//   float  → TAG_FLOAT
//   string → TAG_STR
//   array  → TAG_ARRAY
//   object → TAG_MAP   (keys always string)

static void _jp_skip_ws(const char* s, size_t n, size_t* pi) {
    while (*pi < n) {
        char c = s[*pi];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') (*pi)++;
        else break;
    }
}

static HexaVal _jp_parse_value(const char* s, size_t n, size_t* pi);

static HexaVal _jp_parse_string(const char* s, size_t n, size_t* pi) {
    if (*pi >= n || s[*pi] != '"') return hexa_str("");
    (*pi)++;
    size_t cap = 64, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) return hexa_str("");
    while (*pi < n) {
        char c = s[*pi];
        if (c == '"') { (*pi)++; buf[len] = 0; return hexa_str_own(buf); }
        if (c == '\\' && *pi + 1 < n) {
            char e = s[*pi + 1];
            char out = 0;
            int consumed = 2;
            switch (e) {
                case '"':  out = '"';  break;
                case '\\': out = '\\'; break;
                case '/':  out = '/';  break;
                case 'n':  out = '\n'; break;
                case 'r':  out = '\r'; break;
                case 't':  out = '\t'; break;
                case 'b':  out = '\b'; break;
                case 'f':  out = '\f'; break;
                case 'u':
                    // Raw passthrough (no unicode expansion) — write literal
                    // \uXXXX as the 6 bytes so round-trip is lossless.
                    if (*pi + 5 < n) {
                        if (len + 6 >= cap) { cap = (cap + 6) * 2; char* nb = (char*)realloc(buf, cap); if (!nb) { free(buf); return hexa_str(""); } buf = nb; }
                        memcpy(buf + len, s + *pi, 6);
                        len += 6;
                        *pi += 6;
                        continue;
                    }
                    out = 'u';
                    break;
                default:   out = e; break;
            }
            if (len + 1 >= cap) { cap *= 2; char* nb = (char*)realloc(buf, cap); if (!nb) { free(buf); return hexa_str(""); } buf = nb; }
            buf[len++] = out;
            *pi += consumed;
            continue;
        }
        if (len + 1 >= cap) { cap *= 2; char* nb = (char*)realloc(buf, cap); if (!nb) { free(buf); return hexa_str(""); } buf = nb; }
        buf[len++] = c;
        (*pi)++;
    }
    buf[len] = 0;
    return hexa_str_own(buf);
}

static HexaVal _jp_parse_number(const char* s, size_t n, size_t* pi) {
    size_t start = *pi;
    int is_float = 0;
    if (*pi < n && (s[*pi] == '-' || s[*pi] == '+')) (*pi)++;
    while (*pi < n) {
        char c = s[*pi];
        if (c >= '0' && c <= '9') { (*pi)++; continue; }
        if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
            is_float = 1;
            (*pi)++;
            continue;
        }
        break;
    }
    size_t k = *pi - start;
    char buf[64];
    if (k >= sizeof(buf)) k = sizeof(buf) - 1;
    memcpy(buf, s + start, k);
    buf[k] = 0;
    if (is_float) return hexa_float(atof(buf));
    return hexa_int((int64_t)strtoll(buf, NULL, 10));
}

static HexaVal _jp_parse_array(const char* s, size_t n, size_t* pi) {
    if (*pi >= n || s[*pi] != '[') return hexa_array_new();
    (*pi)++;
    HexaVal arr = hexa_array_new();
    _jp_skip_ws(s, n, pi);
    if (*pi < n && s[*pi] == ']') { (*pi)++; return arr; }
    while (*pi < n) {
        _jp_skip_ws(s, n, pi);
        HexaVal v = _jp_parse_value(s, n, pi);
        arr = hexa_array_push(arr, v);
        _jp_skip_ws(s, n, pi);
        if (*pi < n && s[*pi] == ',') { (*pi)++; continue; }
        if (*pi < n && s[*pi] == ']') { (*pi)++; break; }
        break;
    }
    return arr;
}

static HexaVal _jp_parse_object(const char* s, size_t n, size_t* pi) {
    if (*pi >= n || s[*pi] != '{') return hexa_map_new();
    (*pi)++;
    HexaVal m = hexa_map_new();
    _jp_skip_ws(s, n, pi);
    if (*pi < n && s[*pi] == '}') { (*pi)++; return m; }
    while (*pi < n) {
        _jp_skip_ws(s, n, pi);
        HexaVal key = _jp_parse_string(s, n, pi);
        _jp_skip_ws(s, n, pi);
        if (*pi < n && s[*pi] == ':') (*pi)++;
        _jp_skip_ws(s, n, pi);
        HexaVal val = _jp_parse_value(s, n, pi);
        if (HX_IS_STR(key) && HX_STR(key)) m = hexa_map_set(m, HX_STR(key), val);
        _jp_skip_ws(s, n, pi);
        if (*pi < n && s[*pi] == ',') { (*pi)++; continue; }
        if (*pi < n && s[*pi] == '}') { (*pi)++; break; }
        break;
    }
    return m;
}

static HexaVal _jp_parse_value(const char* s, size_t n, size_t* pi) {
    _jp_skip_ws(s, n, pi);
    if (*pi >= n) return hexa_void();
    char c = s[*pi];
    if (c == '"') return _jp_parse_string(s, n, pi);
    if (c == '{') return _jp_parse_object(s, n, pi);
    if (c == '[') return _jp_parse_array(s, n, pi);
    if (c == 't' && *pi + 3 < n && memcmp(s + *pi, "true", 4) == 0) { *pi += 4; return hexa_bool(1); }
    if (c == 'f' && *pi + 4 < n && memcmp(s + *pi, "false", 5) == 0) { *pi += 5; return hexa_bool(0); }
    if (c == 'n' && *pi + 3 < n && memcmp(s + *pi, "null", 4) == 0) { *pi += 4; return hexa_void(); }
    if (c == '-' || (c >= '0' && c <= '9')) return _jp_parse_number(s, n, pi);
    (*pi)++;  // skip unknown
    return hexa_void();
}

HexaVal hexa_json_parse(HexaVal s) {
    if (!HX_IS_STR(s) || !HX_STR(s)) return hexa_void();
    const char* cs = HX_STR(s);
    size_t n = strlen(cs);
    size_t i = 0;
    return _jp_parse_value(cs, n, &i);
}

HexaVal hexa_json_decode(HexaVal s) { return hexa_json_parse(s); }

// Stringify — one-shot growth buffer, no intermediate HexaVals.
static void _js_buf_reserve(char** pbuf, size_t* pcap, size_t need) {
    if (*pcap >= need) return;
    size_t nc = *pcap ? *pcap : 64;
    while (nc < need) nc *= 2;
    char* nb = (char*)realloc(*pbuf, nc);
    if (!nb) return;
    *pbuf = nb;
    *pcap = nc;
}

static void _js_buf_append(char** pbuf, size_t* pcap, size_t* plen, const char* s, size_t n) {
    _js_buf_reserve(pbuf, pcap, *plen + n + 1);
    if (!*pbuf) return;
    memcpy(*pbuf + *plen, s, n);
    *plen += n;
    (*pbuf)[*plen] = 0;
}

static void _js_emit_string(char** pbuf, size_t* pcap, size_t* plen, const char* s) {
    _js_buf_append(pbuf, pcap, plen, "\"", 1);
    if (!s) { _js_buf_append(pbuf, pcap, plen, "\"", 1); return; }
    for (const char* p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"')       _js_buf_append(pbuf, pcap, plen, "\\\"", 2);
        else if (c == '\\') _js_buf_append(pbuf, pcap, plen, "\\\\", 2);
        else if (c == '\n') _js_buf_append(pbuf, pcap, plen, "\\n", 2);
        else if (c == '\r') _js_buf_append(pbuf, pcap, plen, "\\r", 2);
        else if (c == '\t') _js_buf_append(pbuf, pcap, plen, "\\t", 2);
        else if (c == '\b') _js_buf_append(pbuf, pcap, plen, "\\b", 2);
        else if (c == '\f') _js_buf_append(pbuf, pcap, plen, "\\f", 2);
        else if (c < 0x20) {
            char esc[8];
            int k = snprintf(esc, sizeof(esc), "\\u%04x", c);
            if (k > 0) _js_buf_append(pbuf, pcap, plen, esc, (size_t)k);
        } else {
            char ch = (char)c;
            _js_buf_append(pbuf, pcap, plen, &ch, 1);
        }
    }
    _js_buf_append(pbuf, pcap, plen, "\"", 1);
}

static void _js_emit_value(char** pbuf, size_t* pcap, size_t* plen, HexaVal v);

static void _js_emit_array(char** pbuf, size_t* pcap, size_t* plen, HexaVal v) {
    _js_buf_append(pbuf, pcap, plen, "[", 1);
    int64_t n = (int64_t)HX_ARR_LEN(v);
    for (int64_t i = 0; i < n; i++) {
        if (i) _js_buf_append(pbuf, pcap, plen, ",", 1);
        _js_emit_value(pbuf, pcap, plen, HX_ARR_ITEMS(v)[i]);
    }
    _js_buf_append(pbuf, pcap, plen, "]", 1);
}

static void _js_emit_object(char** pbuf, size_t* pcap, size_t* plen, HexaVal v) {
    _js_buf_append(pbuf, pcap, plen, "{", 1);
    HexaVal keys = hexa_map_keys(v);
    int64_t n = (int64_t)HX_ARR_LEN(keys);
    for (int64_t i = 0; i < n; i++) {
        if (i) _js_buf_append(pbuf, pcap, plen, ",", 1);
        HexaVal k = HX_ARR_ITEMS(keys)[i];
        if (HX_IS_STR(k)) _js_emit_string(pbuf, pcap, plen, HX_STR(k));
        else              _js_emit_string(pbuf, pcap, plen, "");
        _js_buf_append(pbuf, pcap, plen, ":", 1);
        HexaVal vv = hexa_map_get(v, HX_IS_STR(k) ? HX_STR(k) : "");
        _js_emit_value(pbuf, pcap, plen, vv);
    }
    _js_buf_append(pbuf, pcap, plen, "}", 1);
}

static void _js_emit_value(char** pbuf, size_t* pcap, size_t* plen, HexaVal v) {
    if (HX_IS_VOID(v))       { _js_buf_append(pbuf, pcap, plen, "null", 4); return; }
    if (HX_IS_BOOL(v))       { if (HX_BOOL(v)) _js_buf_append(pbuf, pcap, plen, "true", 4); else _js_buf_append(pbuf, pcap, plen, "false", 5); return; }
    if (HX_IS_INT(v))        { char nb[32]; int k = snprintf(nb, sizeof(nb), "%lld", (long long)HX_INT(v)); if (k > 0) _js_buf_append(pbuf, pcap, plen, nb, (size_t)k); return; }
    if (HX_IS_FLOAT(v))      {
        double f = HX_FLOAT(v);
        char nb[64];
        int k;
        if (f != f) k = snprintf(nb, sizeof(nb), "null");
        else if (f == (double)(int64_t)f) k = snprintf(nb, sizeof(nb), "%lld.0", (long long)f);
        else k = snprintf(nb, sizeof(nb), "%.17g", f);
        if (k > 0) _js_buf_append(pbuf, pcap, plen, nb, (size_t)k);
        return;
    }
    if (HX_IS_STR(v))        { _js_emit_string(pbuf, pcap, plen, HX_STR(v) ? HX_STR(v) : ""); return; }
    if (HX_IS_ARRAY(v))      { _js_emit_array(pbuf, pcap, plen, v); return; }
    if (HX_IS_MAP(v))        { _js_emit_object(pbuf, pcap, plen, v); return; }
    _js_buf_append(pbuf, pcap, plen, "null", 4);
}

HexaVal hexa_json_stringify(HexaVal v) {
    char* buf = NULL;
    size_t cap = 0, len = 0;
    _js_emit_value(&buf, &cap, &len, v);
    if (!buf) return hexa_str("");
    return hexa_str_own(buf);
}

HexaVal hexa_json_encode(HexaVal v) { return hexa_json_stringify(v); }

// FIX-A (Anima ML eval unblock, 2026-04-19) ─────────────────────────
// 8 tensor_* fallback builtins required by anima/serving/eval_alm.hexa
// (and siblings) that do not explicitly `use "self/ml/tensor_ops.hexa"`.
// Semantics mirror the pure-hexa tensor_ops implementation; performance
// is intentionally naive — callers on the fast-path still pull the
// BLAS/NEON-optimized self/ml/ modules explicitly. All ops operate on
// flat row-major arrays of TAG_FLOAT / TAG_INT (coerced via __hx_to_double).

// tensor_zeros(n): length-n array filled with 0.0.
HexaVal hexa_tensor_zeros(HexaVal nv) {
    int64_t n = (int64_t)__hx_to_double(nv);
    if (n < 0) n = 0;
    HexaVal arr = hexa_array_new();
    for (int64_t i = 0; i < n; i++) arr = hexa_array_push(arr, hexa_float(0.0));
    return arr;
}

// FIX-A 2nd batch (2026-04-19): tensor_ones(n) — length-n array filled with 1.0.
// Pairs with tensor_zeros for anima-speak init paths (ln_g, rms_g).
HexaVal hexa_tensor_ones(HexaVal nv) {
    int64_t n = (int64_t)__hx_to_double(nv);
    if (n < 0) n = 0;
    HexaVal arr = hexa_array_new();
    for (int64_t i = 0; i < n; i++) arr = hexa_array_push(arr, hexa_float(1.0));
    return arr;
}

// FIX-A 2nd batch (2026-04-19): swiglu_vec(gate, up) = silu(gate) * up elementwise.
// silu(x) = x / (1 + exp(-x)). Shape: min(|gate|, |up|). For anima eval_alm.hexa
// + training hot paths (nn_core etc.) — scalar fallback, fast-path is FFN fused kernel.
HexaVal hexa_swiglu_vec(HexaVal gate, HexaVal up) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(gate) || !HX_IS_ARRAY(up)) return out;
    int64_t ng = (int64_t)HX_ARR_LEN(gate), nu = (int64_t)HX_ARR_LEN(up);
    int64_t k = ng < nu ? ng : nu;
    for (int64_t i = 0; i < k; i++) {
        double g = __hx_to_double(hexa_array_get(gate, i));
        double u = __hx_to_double(hexa_array_get(up, i));
        double silu = g / (1.0 + exp(-g));
        out = hexa_array_push(out, hexa_float(silu * u));
    }
    return out;
}

// tensor_slice(arr, lo, hi): subarray [lo, hi) with clamped bounds.
HexaVal hexa_tensor_slice(HexaVal a, HexaVal lov, HexaVal hiv) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(a)) return out;
    int64_t n = (int64_t)HX_ARR_LEN(a);
    int64_t lo = (int64_t)__hx_to_double(lov);
    int64_t hi = (int64_t)__hx_to_double(hiv);
    if (lo < 0) lo = 0;
    if (hi > n) hi = n;
    for (int64_t i = lo; i < hi; i++) out = hexa_array_push(out, hexa_array_get(a, i));
    return out;
}

// tensor_add(a, b): elementwise a[i] + b[i], length = min(|a|, |b|).
HexaVal hexa_tensor_add(HexaVal a, HexaVal b) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(a) || !HX_IS_ARRAY(b)) return out;
    int64_t na = (int64_t)HX_ARR_LEN(a), nb = (int64_t)HX_ARR_LEN(b);
    int64_t k = na < nb ? na : nb;
    for (int64_t i = 0; i < k; i++) {
        double va = __hx_to_double(hexa_array_get(a, i));
        double vb = __hx_to_double(hexa_array_get(b, i));
        out = hexa_array_push(out, hexa_float(va + vb));
    }
    return out;
}

// tensor_dot(a, b): scalar sum(a[i] * b[i]) over min length.
HexaVal hexa_tensor_dot(HexaVal a, HexaVal b) {
    if (!HX_IS_ARRAY(a) || !HX_IS_ARRAY(b)) return hexa_float(0.0);
    int64_t na = (int64_t)HX_ARR_LEN(a), nb = (int64_t)HX_ARR_LEN(b);
    int64_t k = na < nb ? na : nb;
    double s = 0.0;
    for (int64_t i = 0; i < k; i++) {
        s += __hx_to_double(hexa_array_get(a, i)) *
             __hx_to_double(hexa_array_get(b, i));
    }
    return hexa_float(s);
}

// tensor_mul_scalar(a, s): elementwise a[i] * s.
HexaVal hexa_tensor_mul_scalar(HexaVal a, HexaVal sv) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(a)) return out;
    int64_t n = (int64_t)HX_ARR_LEN(a);
    double s = __hx_to_double(sv);
    for (int64_t i = 0; i < n; i++) {
        double va = __hx_to_double(hexa_array_get(a, i));
        out = hexa_array_push(out, hexa_float(va * s));
    }
    return out;
}

// hadamard(a, b): elementwise product (same semantics as tensor_add but *).
// Matches interpreter hexa_full.hexa:10496 behavior.
HexaVal hexa_hadamard(HexaVal a, HexaVal b) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(a) || !HX_IS_ARRAY(b)) return out;
    int64_t na = (int64_t)HX_ARR_LEN(a), nb = (int64_t)HX_ARR_LEN(b);
    int64_t k = na < nb ? na : nb;
    for (int64_t i = 0; i < k; i++) {
        double va = __hx_to_double(hexa_array_get(a, i));
        double vb = __hx_to_double(hexa_array_get(b, i));
        out = hexa_array_push(out, hexa_float(va * vb));
    }
    return out;
}

// silu(a): elementwise SiLU (Swish-1) activation: x / (1 + exp(-x)).
// Matches interpreter silu path (standalone — used by SwiGLU decomposition).
HexaVal hexa_silu(HexaVal a) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(a)) return out;
    int64_t n = (int64_t)HX_ARR_LEN(a);
    for (int64_t i = 0; i < n; i++) {
        double x = __hx_to_double(hexa_array_get(a, i));
        out = hexa_array_push(out, hexa_float(x / (1.0 + exp(-x))));
    }
    return out;
}

// gelu(a): elementwise Gaussian Error Linear Unit (tanh approximation).
// Matches interpreter hexa_full.hexa:10108 — 0.5*x*(1+tanh(sqrt(2/pi)*(x+0.044715*x^3))).
HexaVal hexa_gelu(HexaVal a) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(a)) return out;
    int64_t n = (int64_t)HX_ARR_LEN(a);
    const double k = 0.7978845608028654; // sqrt(2/pi)
    for (int64_t i = 0; i < n; i++) {
        double x = __hx_to_double(hexa_array_get(a, i));
        double inner = k * (x + 0.044715 * x * x * x);
        out = hexa_array_push(out, hexa_float(0.5 * x * (1.0 + tanh(inner))));
    }
    return out;
}

// argmax(a): return index of largest element (ties → lowest index).
// Matches interpreter hexa_full.hexa:9616 — int result.
HexaVal hexa_argmax(HexaVal a) {
    if (!HX_IS_ARRAY(a)) return hexa_int(-1);
    int64_t n = (int64_t)HX_ARR_LEN(a);
    if (n == 0) return hexa_int(-1);
    int64_t best_i = 0;
    double best_v = __hx_to_double(hexa_array_get(a, 0));
    for (int64_t i = 1; i < n; i++) {
        double v = __hx_to_double(hexa_array_get(a, i));
        if (v > best_v) { best_v = v; best_i = i; }
    }
    return hexa_int(best_i);
}

// sum(a): reduce-sum; returns int if all elements int, float otherwise.
// Matches interpreter hexa_full.hexa:13232.
HexaVal hexa_sum(HexaVal a) {
    if (!HX_IS_ARRAY(a)) return hexa_int(0);
    int64_t n = (int64_t)HX_ARR_LEN(a);
    int has_float = 0;
    int64_t int_total = 0;
    double float_total = 0.0;
    for (int64_t i = 0; i < n; i++) {
        HexaVal e = hexa_array_get(a, i);
        if (HX_IS_FLOAT(e)) { has_float = 1; float_total += HX_FLOAT(e); }
        else { int_total += HX_INT(e); }
    }
    if (has_float) return hexa_float((double)int_total + float_total);
    return hexa_int(int_total);
}

// clamp(x, lo, hi): scalar clamp, float result.
// Matches interpreter hexa_full.hexa:13468.
HexaVal hexa_clamp(HexaVal xv, HexaVal lov, HexaVal hiv) {
    double x = __hx_to_double(xv);
    double lo = __hx_to_double(lov);
    double hi = __hx_to_double(hiv);
    if (x < lo) return hexa_float(lo);
    if (x > hi) return hexa_float(hi);
    return hexa_float(x);
}

// one_hot(idx, n): n-length binary vector, 1.0 at idx, 0.0 elsewhere.
// Matches interpreter hexa_full.hexa:10512.
HexaVal hexa_one_hot(HexaVal idxv, HexaVal nv) {
    int64_t idx = HX_IS_INT(idxv) ? HX_INT(idxv) : (int64_t)__hx_to_double(idxv);
    int64_t n = HX_IS_INT(nv) ? HX_INT(nv) : (int64_t)__hx_to_double(nv);
    HexaVal out = hexa_array_new();
    for (int64_t i = 0; i < n; i++) {
        out = hexa_array_push(out, hexa_float(i == idx ? 1.0 : 0.0));
    }
    return out;
}

// rms_norm(x, gamma, eps): gamma[i] * x[i] / sqrt(mean(x^2) + eps).
// gamma may be array (per-element scale) or scalar (uniform scale).
HexaVal hexa_rms_norm(HexaVal x, HexaVal gamma, HexaVal epsv) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(x)) return out;
    int64_t n = (int64_t)HX_ARR_LEN(x);
    if (n == 0) return out;
    double ss = 0.0;
    for (int64_t i = 0; i < n; i++) {
        double v = __hx_to_double(hexa_array_get(x, i));
        ss += v * v;
    }
    double mean = ss / (double)n;
    double eps = __hx_to_double(epsv);
    double inv = 1.0 / sqrt(mean + eps);
    int gamma_is_arr = HX_IS_ARRAY(gamma);
    double gamma_scalar = gamma_is_arr ? 1.0 : __hx_to_double(gamma);
    for (int64_t i = 0; i < n; i++) {
        double v = __hx_to_double(hexa_array_get(x, i));
        double g = gamma_is_arr
            ? __hx_to_double(hexa_array_get(gamma, i < (int64_t)HX_ARR_LEN(gamma) ? i : 0))
            : gamma_scalar;
        out = hexa_array_push(out, hexa_float(g * v * inv));
    }
    return out;
}

// softmax(a): stable softmax — subtract max, exp, normalize by sum.
HexaVal hexa_softmax(HexaVal a) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(a)) return out;
    int64_t n = (int64_t)HX_ARR_LEN(a);
    if (n == 0) return out;
    double m = __hx_to_double(hexa_array_get(a, 0));
    for (int64_t i = 1; i < n; i++) {
        double v = __hx_to_double(hexa_array_get(a, i));
        if (v > m) m = v;
    }
    double sum = 0.0;
    double* tmp = (double*)malloc(sizeof(double) * (size_t)n);
    for (int64_t i = 0; i < n; i++) {
        double v = __hx_to_double(hexa_array_get(a, i));
        tmp[i] = exp(v - m);
        sum += tmp[i];
    }
    double inv = sum > 0.0 ? 1.0 / sum : 0.0;
    for (int64_t i = 0; i < n; i++) out = hexa_array_push(out, hexa_float(tmp[i] * inv));
    free(tmp);
    return out;
}

// matmul(A, B, m, k, n): row-major C[m*n] = A[m*k] @ B[k*n], naive O(mkn).
HexaVal hexa_matmul(HexaVal a, HexaVal b, HexaVal mv, HexaVal kv, HexaVal nv) {
    HexaVal out = hexa_array_new();
    if (!HX_IS_ARRAY(a) || !HX_IS_ARRAY(b)) return out;
    int64_t m = (int64_t)__hx_to_double(mv);
    int64_t k = (int64_t)__hx_to_double(kv);
    int64_t n = (int64_t)__hx_to_double(nv);
    if (m < 0 || k < 0 || n < 0) return out;
    int64_t alen = (int64_t)HX_ARR_LEN(a), blen = (int64_t)HX_ARR_LEN(b);
    for (int64_t i = 0; i < m; i++) {
        for (int64_t j = 0; j < n; j++) {
            double s = 0.0;
            for (int64_t p = 0; p < k; p++) {
                int64_t ai = i * k + p;
                int64_t bi = p * n + j;
                if (ai >= alen || bi >= blen) continue;
                s += __hx_to_double(hexa_array_get(a, ai)) *
                     __hx_to_double(hexa_array_get(b, bi));
            }
            out = hexa_array_push(out, hexa_float(s));
        }
    }
    return out;
}

// Base64 (RFC 4648)
static const char _b64_enc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

HexaVal hexa_base64_encode(HexaVal s) {
    if (!HX_IS_STR(s) || !HX_STR(s)) return hexa_str("");
    const unsigned char* in = (const unsigned char*)HX_STR(s);
    size_t n = strlen(HX_STR(s));
    size_t olen = 4 * ((n + 2) / 3);
    char* out = (char*)malloc(olen + 1);
    size_t i = 0, j = 0;
    while (i + 3 <= n) {
        uint32_t t = (in[i] << 16) | (in[i+1] << 8) | in[i+2];
        out[j++] = _b64_enc[(t >> 18) & 0x3F];
        out[j++] = _b64_enc[(t >> 12) & 0x3F];
        out[j++] = _b64_enc[(t >> 6) & 0x3F];
        out[j++] = _b64_enc[t & 0x3F];
        i += 3;
    }
    if (i < n) {
        uint32_t t = in[i] << 16;
        int rem = (int)(n - i);
        if (rem == 2) t |= in[i+1] << 8;
        out[j++] = _b64_enc[(t >> 18) & 0x3F];
        out[j++] = _b64_enc[(t >> 12) & 0x3F];
        out[j++] = (rem == 2) ? _b64_enc[(t >> 6) & 0x3F] : '=';
        out[j++] = '=';
    }
    out[j] = 0;
    return hexa_str_own(out);
}

HexaVal hexa_base64_decode(HexaVal s) {
    if (!HX_IS_STR(s) || !HX_STR(s)) return hexa_str("");
    static int dec[256];
    static int dec_init = 0;
    if (!dec_init) {
        for (int i = 0; i < 256; i++) dec[i] = -1;
        for (int i = 0; i < 64; i++) dec[(unsigned char)_b64_enc[i]] = i;
        dec_init = 1;
    }
    const unsigned char* in = (const unsigned char*)HX_STR(s);
    size_t n = strlen(HX_STR(s));
    char* out = (char*)malloc(n + 1);
    size_t j = 0;
    int bits = 0, vbits = 0;
    for (size_t i = 0; i < n; i++) {
        if (in[i] == '=') break;
        int v = dec[in[i]];
        if (v < 0) continue;
        bits = (bits << 6) | v;
        vbits += 6;
        if (vbits >= 8) {
            vbits -= 8;
            out[j++] = (char)((bits >> vbits) & 0xFF);
        }
    }
    out[j] = 0;
    return hexa_str_own(out);
}

// ── bt 73: bare-ident HexaVal globals for transpiled builtin dispatch ──
// Self-host codegen (self/native/hexa_cc.c gen2_expr Call fallback, ~line 5642+)
// emits `hexa_call0(timestamp)`, `hexa_call1(base64_encode, s)`, … where the
// callee is a BARE C identifier. For the linker to resolve these, each name
// must exist as a `HexaVal` variable holding a TAG_FN that wraps the real
// implementation. bt 71 supplied the hexa_*-prefixed C functions; this block
// wires them to the bare names the transpiler expects.
//
// hexa_call0/hexa_call1 (defined above in this file) already branch on
// TAG_FN and cast fn_ptr to `HexaVal (*)(…)`, so HexaVal-returning wrappers
// with matching arity work directly.
//
// Wrapper for `timestamp` — hexa_timestamp is declared above without params;
// expose under the void→HexaVal signature hexa_call0 expects.
static HexaVal _bt73_timestamp_w(void) { return hexa_timestamp(); }
static HexaVal _bt73_base64_encode_w(HexaVal s) { return hexa_base64_encode(s); }
static HexaVal _bt73_base64_decode_w(HexaVal s) { return hexa_base64_decode(s); }
// interp bootstrap gap: hexa_v2 (already-baked transpiler) doesn't know
// about setenv / exec_capture, so the interpreter dispatch in hexa_full.hexa
// resolves bare `setenv(...)` through hexa_call2 / `exec_capture(...)` via
// hexa_call1. Shim these as TAG_FN globals so the linker finds them.
static HexaVal _w_setenv(HexaVal n, HexaVal v) { return hexa_setenv(n, v); }
static HexaVal _w_exec_capture(HexaVal c) { return hexa_exec_capture(c); }
static HexaVal _w_push(HexaVal a, HexaVal v) { return hexa_array_push(a, v); }

HexaVal timestamp;
HexaVal base64_encode;
HexaVal base64_decode;
// Names intentionally prefixed — plain `setenv` would clobber the libc
// prototype from <stdlib.h>, and `exec_capture` is new surface.
HexaVal hx_setenv;
HexaVal hx_exec_capture;
HexaVal hx_push;

// S1-D2 Blocker C: runtime init for TAG_FN shim variables.
// NaN-boxing makes HexaVal a uint64_t — designated initializers for the
// struct layout are illegal.  Lazy-init mirrors _hexa_init_cached_strs().
static int _fn_shims_ready = 0;
static void _hexa_init_net_fn_shims(void);  // fwd decl — body in native/net.c
static void _hexa_init_os_fn_shims(void);   // fwd decl — body in native/signal_flock.c
static void _hexa_init_exec_sha_fn_shims(void); // fwd decl — body in native/exec_argv_sha256.c
static void _hexa_init_persistent_pipe_fn_shims(void); // fwd decl — body in native/persistent_pipe.c
static void _hexa_init_fn_shims(void) {
    if (_fn_shims_ready) return;
    // bootstrap free-fn shims (join, char_code, chr, bit_or)
    // (`split` was retired 2026-04-21 — codegen_c2.hexa now emits hexa_str_split directly.)
    join       = hexa_fn_new((void*)hexa_str_join,        2);
    char_code  = hexa_fn_new((void*)hexa_char_code,       2);
    chr        = hexa_fn_new((void*)hexa_from_char_code,   1);
    bit_or     = hexa_fn_new((void*)_hx_bit_or,           2);
    // bt73 free-fn shims (timestamp, base64_encode, base64_decode)
    timestamp     = hexa_fn_new((void*)_bt73_timestamp_w,     0);
    base64_encode = hexa_fn_new((void*)_bt73_base64_encode_w, 1);
    base64_decode = hexa_fn_new((void*)_bt73_base64_decode_w, 1);
    // interp exec/env primitives — bootstrap-gap bridge
    hx_setenv       = hexa_fn_new((void*)_w_setenv,       2);
    hx_exec_capture = hexa_fn_new((void*)_w_exec_capture, 1);
    // hxa-004 ext: bare `push(arr, v)` emitted by legacy hexa_v2 as hexa_call2
    hx_push         = hexa_fn_new((void*)_w_push,         2);
    // std::net free-fn shims — bridges transpiler bootstrap gap for
    // net_connect / net_read / net_write until hexa_v2 learns the
    // direct-lowering for these names (see native/net.c comment).
    _hexa_init_net_fn_shims();
    // stdlib/os signal + flock free-fn shims (roadmap 62, 2026-04-22)
    _hexa_init_os_fn_shims();
    // hxa-20260424-005 items #1 + #7: exec_argv + sha256 shims
    _hexa_init_exec_sha_fn_shims();
    // ω-runtime-1 Phase A (2026-04-26): persistent-pipe primitive shims
    _hexa_init_persistent_pipe_fn_shims();
    _fn_shims_ready = 1;
}

/* ═══════════════════════════════════════════════════════════════════
 * T3 HOT KERNEL INCLUDE — anima training inner-loop fast path.
 *
 * f32/f64/i32 ptr r/w + struct_pack_f32/unpack_f32 + tensor stubs are
 * extracted into native/tensor_kernels.c so anima maintainers can find
 * the performance-critical boundary without grep'ing through 3600 LOC.
 *
 * @hot_kernel markers inside the file describe each function's role.
 * Whitelist (external consumers): $NEXUS/shared/hexa/hot-path.jsonl.
 * Migration policy: docs/hexa-hot-path.md.
 *
 * NEVER migrate these to HEXA — per-element HexaVal wrap/dispatch would
 * multiply training time 10-100x. Any future HEXA reimplementation must
 * preserve raw C float* access patterns.
 * ═══════════════════════════════════════════════════════════════════ */
#include "native/tensor_kernels.c"

/* ═══════════════════════════════════════════════════════════════════
 * std::net — POSIX TCP socket builtins (interp resurrection, 2026-04-16)
 *
 * Ports the networking surface from the deleted Rust src/std_net.rs
 * (recovered from commit ef92fc6). 6 primitives; http_get / http_serve
 * are composed on top of them in self/std_net.hexa.
 *
 *   hexa_net_listen(addr)       : string "host:port" → TAG_INT fd / -errno
 *   hexa_net_accept(listen)     : TAG_INT listen_fd  → TAG_INT client_fd
 *   hexa_net_connect(addr)      : string "host:port" → TAG_INT fd / -errno
 *   hexa_net_read(fd)           : TAG_INT fd         → TAG_STR data
 *   hexa_net_write(fd, data)    : TAG_INT fd, string → TAG_INT bytes / -errno
 *   hexa_net_close(fd)          : TAG_INT fd         → TAG_INT 0 / -errno
 *
 * Error model: negative TAG_INT (the raw errno). Matches the C-side
 * convention used by hxcuda/hxblas shims.
 *
 * Implementation lives in native/net.c so the net subsystem is a single
 * file analogous to native/tensor_kernels.c for the hot kernel path.
 * ═══════════════════════════════════════════════════════════════════ */
#include "native/net.c"

/* ═══════════════════════════════════════════════════════════════════
 * B20 / roadmap 55 Phase 1 — deterministic FP control-word init.
 *
 * Exposes void hexa_fp_init(void). codegen_c2.hexa emits a call to it
 * as the first statement of generated main(). Implementation in
 * self/native/fp_init.c normalizes MXCSR (x86_64) or FPCR (aarch64)
 * to IEEE-default behavior (no FTZ/DAZ/FZ, round-to-nearest-even) so
 * @strict_fp functions can rely on bit-exact FP semantics.
 *
 * Phase 2 scope: crlibm vendoring for cross-substrate transcendental
 * identity; pthread_create trampoline. Not shipped in Phase 1.
 * ═══════════════════════════════════════════════════════════════════ */
#include "native/fp_init.c"

/* ═══════════════════════════════════════════════════════════════════
 * stdlib/os — signal + flock builtins (roadmap 62, 2026-04-22)
 *
 * Exposes hexa_os_sig_* (install / raise / drain / block / …) and
 * hexa_os_flock_* (open / close) for the stdlib/os/{signal,flock}.hexa
 * modules. Async-signal-safe self-pipe trampoline; O_CLOEXEC on flock
 * fds so execve(3) does not leak locks into children.
 *
 * Implementation: native/signal_flock.c (see header comment for full
 * contract + semantics).
 * ═══════════════════════════════════════════════════════════════════ */
#include "native/signal_flock.c"

/* ═══════════════════════════════════════════════════════════════════
 * hxa-20260424-005 items #1 + #7 — exec_argv + sha256 (2026-04-24)
 *
 * Exposes:
 *   hexa_exec_argv(argv_arr)             : fork/execvp, no /bin/sh -c
 *   hexa_exec_argv_with_status(argv_arr) : same + exit code
 *   hexa_sha256(s)                       : lowercase hex digest
 *   hexa_sha256_file(path)               : lowercase hex digest of file
 *
 * Implementation: native/exec_argv_sha256.c (pure libc + FIPS 180-4
 * reference sha256). TAG_FN shims registered from _hexa_init_fn_shims
 * so the interpreter path resolves `exec_argv`/`sha256` names before
 * the transpiler learns direct-lowering.
 * ═══════════════════════════════════════════════════════════════════ */
#include "native/exec_argv_sha256.c"

/* ═══════════════════════════════════════════════════════════════════
 * persistent_pipe — ω-runtime-1 Phase A (2026-04-26)
 *
 * Handle-based bidirectional child-process API. Five new builtins:
 *   pipe_spawn(cmd) -> int
 *   pipe_send_line(h, payload) -> bool
 *   pipe_recv_line(h, timeout_ms) -> string
 *   pipe_close(h) -> bool
 *   pipe_alive(h) -> bool
 * Powers ω-audio-2 60× chain target (replaces _audio_worker_call.sh
 * 133-line bash shim with sub-ms send/recv via persistent fork).
 * ═══════════════════════════════════════════════════════════════════ */
#include "native/persistent_pipe.c"

