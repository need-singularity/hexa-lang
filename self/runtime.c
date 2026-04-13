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

// ═══════════════════════════════════════════════════════════
//  Optimization #11: String Interning
//  Hash-set of unique strings. Short strings (< 64 chars)
//  are interned; comparison becomes pointer equality (==).
//  Lazily initialized on first hexa_intern() call.
// ═══════════════════════════════════════════════════════════

#define INTERN_INIT_CAP   256
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
typedef struct HexaVal HexaVal;
HexaVal hexa_add(HexaVal a, HexaVal b);
HexaVal hexa_sub(HexaVal a, HexaVal b);
HexaVal hexa_mul(HexaVal a, HexaVal b);
HexaVal hexa_div(HexaVal a, HexaVal b);
HexaVal hexa_mod(HexaVal a, HexaVal b);
HexaVal hexa_eq(HexaVal a, HexaVal b);
HexaVal hexa_to_string(HexaVal v);
HexaVal hexa_str_concat(HexaVal a, HexaVal b);
HexaVal hexa_abs(HexaVal v);
HexaVal hexa_pad_left(HexaVal s, HexaVal width);
HexaVal hexa_pad_right(HexaVal s, HexaVal width);
HexaVal hexa_str_repeat(HexaVal s, HexaVal n);
HexaVal hexa_read_file(HexaVal path);
HexaVal hexa_write_file(HexaVal path, HexaVal content);
HexaVal hexa_str_split(HexaVal s, HexaVal delim);
HexaVal hexa_str_trim(HexaVal s);
HexaVal hexa_str_replace(HexaVal s, HexaVal old, HexaVal new_s);
HexaVal hexa_str_join(HexaVal arr, HexaVal sep);
HexaVal hexa_str_to_upper(HexaVal s);
HexaVal hexa_str_to_lower(HexaVal s);
HexaVal hexa_str_chars(HexaVal s);
HexaVal hexa_format_n(HexaVal fmt, HexaVal args);
HexaVal hexa_str_parse_int(HexaVal s);
HexaVal hexa_str_parse_float(HexaVal s);
HexaVal hexa_str_trim_start(HexaVal s);
HexaVal hexa_str_trim_end(HexaVal s);
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

// ── Tagged Value ─────────────────────────────────────────
// All HEXA values are represented as tagged unions (NaN-boxing alternative)

typedef enum {
    TAG_INT = 0, TAG_FLOAT, TAG_BOOL, TAG_STR, TAG_VOID,
    TAG_ARRAY, TAG_MAP, TAG_FN, TAG_CHAR, TAG_CLOSURE
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
} HexaMapSlot;

typedef struct {
    HexaMapSlot* slots;   // hash table (power-of-2 sized)
    struct HexaVal* vals;  // parallel values array (same indices as slots)
    int ht_cap;            // hash table capacity (power-of-2)

    // Insertion-order arrays for keys()/values()/for-in iteration
    char** order_keys;     // ordered key pointers (point into slots[].key)
    struct HexaVal* order_vals; // ordered values
    int len;               // number of entries
    int order_cap;         // allocated capacity for order arrays
} HexaMapTable;

typedef struct HexaVal {
    HexaTag tag;
    union {
        int64_t i;
        double f;
        int b;
        char* s;         // owned string (malloc'd)
        struct {
            struct HexaVal* items;
            int len;
            int cap;
        } arr;
        struct {
            HexaMapTable* tbl;   // heap-allocated hash table
            int len;             // cached count (== tbl->len when tbl != NULL)
        } map;
        struct {
            void* fn_ptr;
            int arity;
        } fn;
        // C3 closure: function pointer that takes (HexaVal env, ...args)
        // env is a boxed TAG_ARRAY HexaVal (heap-allocated so it survives copies).
        struct {
            void* fn_ptr;           // signature: HexaVal (*)(HexaVal env, ...N args)
            int arity;              // number of user params (excluding env)
            struct HexaVal* env_box;// heap box holding a single TAG_ARRAY HexaVal
        } clo;
    };
} HexaVal;

// ── C3 Closure helpers ───────────────────────────────────
// Build a closure value. Captured values are provided as an already-built
// TAG_ARRAY HexaVal; we heap-box it so the closure remains valid after copies.
static inline HexaVal hexa_closure_new(void* fn_ptr, int arity, HexaVal env_arr) {
    HexaVal v = {.tag=TAG_CLOSURE};
    v.clo.fn_ptr = fn_ptr;
    v.clo.arity = arity;
    v.clo.env_box = (HexaVal*)malloc(sizeof(HexaVal));
    *v.clo.env_box = env_arr;
    return v;
}

static inline HexaVal hexa_closure_env(HexaVal c) {
    if (c.tag != TAG_CLOSURE || !c.clo.env_box) return hexa_array_new();
    return *c.clo.env_box;
}

// Dispatched call helpers — one per arity we support (0..4).
static inline HexaVal hexa_call0(HexaVal f) {
    if (f.tag == TAG_CLOSURE) {
        HexaVal (*fp)(HexaVal) = (HexaVal(*)(HexaVal))f.clo.fn_ptr;
        return fp(hexa_closure_env(f));
    }
    if (f.tag == TAG_FN) {
        HexaVal (*fp)(void) = (HexaVal(*)(void))f.fn.fn_ptr;
        return fp();
    }
    return hexa_void();
}
static inline HexaVal hexa_call1(HexaVal f, HexaVal a1) {
    if (f.tag == TAG_CLOSURE) {
        HexaVal (*fp)(HexaVal, HexaVal) = (HexaVal(*)(HexaVal, HexaVal))f.clo.fn_ptr;
        return fp(hexa_closure_env(f), a1);
    }
    if (f.tag == TAG_FN) {
        HexaVal (*fp)(HexaVal) = (HexaVal(*)(HexaVal))f.fn.fn_ptr;
        return fp(a1);
    }
    return hexa_void();
}
static inline HexaVal hexa_call2(HexaVal f, HexaVal a1, HexaVal a2) {
    if (f.tag == TAG_CLOSURE) {
        HexaVal (*fp)(HexaVal, HexaVal, HexaVal) = (HexaVal(*)(HexaVal, HexaVal, HexaVal))f.clo.fn_ptr;
        return fp(hexa_closure_env(f), a1, a2);
    }
    if (f.tag == TAG_FN) {
        HexaVal (*fp)(HexaVal, HexaVal) = (HexaVal(*)(HexaVal, HexaVal))f.fn.fn_ptr;
        return fp(a1, a2);
    }
    return hexa_void();
}
static inline HexaVal hexa_call3(HexaVal f, HexaVal a1, HexaVal a2, HexaVal a3) {
    if (f.tag == TAG_CLOSURE) {
        HexaVal (*fp)(HexaVal, HexaVal, HexaVal, HexaVal) = (HexaVal(*)(HexaVal, HexaVal, HexaVal, HexaVal))f.clo.fn_ptr;
        return fp(hexa_closure_env(f), a1, a2, a3);
    }
    if (f.tag == TAG_FN) {
        HexaVal (*fp)(HexaVal, HexaVal, HexaVal) = (HexaVal(*)(HexaVal, HexaVal, HexaVal))f.fn.fn_ptr;
        return fp(a1, a2, a3);
    }
    return hexa_void();
}
static inline HexaVal hexa_call4(HexaVal f, HexaVal a1, HexaVal a2, HexaVal a3, HexaVal a4) {
    if (f.tag == TAG_CLOSURE) {
        HexaVal (*fp)(HexaVal, HexaVal, HexaVal, HexaVal, HexaVal) = (HexaVal(*)(HexaVal, HexaVal, HexaVal, HexaVal, HexaVal))f.clo.fn_ptr;
        return fp(hexa_closure_env(f), a1, a2, a3, a4);
    }
    if (f.tag == TAG_FN) {
        HexaVal (*fp)(HexaVal, HexaVal, HexaVal, HexaVal) = (HexaVal(*)(HexaVal, HexaVal, HexaVal, HexaVal))f.fn.fn_ptr;
        return fp(a1, a2, a3, a4);
    }
    return hexa_void();
}

// ── Constructors ─────────────────────────────────────────

HexaVal hexa_int(int64_t n) { return (HexaVal){.tag=TAG_INT, .i=n}; }
HexaVal hexa_float(double f) { return (HexaVal){.tag=TAG_FLOAT, .f=f}; }
HexaVal hexa_bool(int b) { return (HexaVal){.tag=TAG_BOOL, .b=b}; }
HexaVal hexa_void() { return (HexaVal){.tag=TAG_VOID}; }

// Null coalescing: a ?? b — if a is void or empty string, return b
HexaVal hexa_null_coal(HexaVal a, HexaVal b) {
    if (a.tag == TAG_VOID) return b;
    if (a.tag == TAG_STR && (a.s == NULL || a.s[0] == '\0')) return b;
    return a;
}

HexaVal hexa_str(const char* s) {
    HexaVal v = {.tag=TAG_STR};
    // Optimization #11: intern short strings for pointer-equality comparison
    const char* interned = hexa_intern(s);
    if (interned) {
        v.s = (char*)interned;  // owned by intern table, not caller
    } else {
        v.s = strdup(s);        // long/unique strings: traditional copy
    }
    return v;
}

HexaVal hexa_str_own(char* s) {
    return (HexaVal){.tag=TAG_STR, .s=s};
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
static int _hx_stats_enabled = -1;             // lazy probe of env

static void _hx_stats_dump(void) {
    fprintf(stderr, "[HEXA_ALLOC_STATS] array_new=%lld push=%lld grow=%lld reserve=%lld str_concat=%lld map_new=%lld map_set=%lld\n",
        (long long)_hx_stats_array_new,
        (long long)_hx_stats_array_push,
        (long long)_hx_stats_array_grow,
        (long long)_hx_stats_array_reserve,
        (long long)_hx_stats_str_concat,
        (long long)_hx_stats_map_new,
        (long long)_hx_stats_map_set);
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

HexaVal hexa_array_new() {
    if (_hx_stats_on()) _hx_stats_array_new++;
    HexaVal v = {.tag=TAG_ARRAY};
    v.arr.items = NULL; v.arr.len = 0; v.arr.cap = 0;
    return v;
}

// Optimization #12: reserve capacity up front when size is known.
HexaVal hexa_array_reserve(HexaVal arr, int n) {
    if (n <= arr.arr.cap) return arr;
    if (_hx_stats_on()) _hx_stats_array_reserve++;
    HexaVal* new_items = realloc(arr.arr.items, sizeof(HexaVal) * n);
    if (!new_items) { fprintf(stderr, "OOM in array_reserve\n"); exit(1); }
    arr.arr.items = new_items;
    arr.arr.cap = n;
    return arr;
}

// Optimization #12: grow from current cap (2x), not from new_len.
// Only realloc when len exceeds cap; growth factor 2x amortizes cost.
HexaVal hexa_array_push(HexaVal arr, HexaVal item) {
    if (_hx_stats_on()) _hx_stats_array_push++;
    if (arr.arr.len >= arr.arr.cap) {
        if (_hx_stats_on()) _hx_stats_array_grow++;
        int new_cap = arr.arr.cap < 8 ? 8 : arr.arr.cap * 2;
        HexaVal* new_items = realloc(arr.arr.items, sizeof(HexaVal) * new_cap);
        if (!new_items) { fprintf(stderr, "OOM in array_push\n"); exit(1); }
        arr.arr.items = new_items;
        arr.arr.cap = new_cap;
    }
    arr.arr.items[arr.arr.len] = item;
    arr.arr.len++;
    return arr;
}

HexaVal hexa_array_get(HexaVal arr, int64_t idx) {
    if (idx < 0) idx += arr.arr.len;
    if (idx < 0 || idx >= arr.arr.len) {
        fprintf(stderr, "index %lld out of bounds (len %d)\n", (long long)idx, arr.arr.len);
        exit(1);
    }
    return arr.arr.items[idx];
}

HexaVal hexa_array_set(HexaVal arr, int64_t idx, HexaVal val) {
    if (idx < 0) idx += arr.arr.len;
    if (idx < 0 || idx >= arr.arr.len) {
        fprintf(stderr, "index %lld out of bounds (len %d)\n", (long long)idx, arr.arr.len);
        exit(1);
    }
    // In-place mutate (no copy — caller reassigns)
    arr.arr.items[idx] = val;
    return arr;
}

// bt 55 — in-place truncate. Used by env_pop_scope to avoid the
// `new_vars.push` rebuild loop which realloc-leaks the old backing buffer
// every scope pop. Retains capacity for amortized re-growth.
// No free of arr.items here (HexaVal entries may still be shared); we only
// shrink the logical length. Safe because the slots beyond `new_len` become
// unreachable through this handle.
HexaVal hexa_array_truncate(HexaVal arr, HexaVal new_len_v) {
    if (arr.tag != TAG_ARRAY) return arr;
    int64_t new_len = (new_len_v.tag == TAG_INT) ? new_len_v.i : (int64_t)new_len_v.i;
    if (new_len < 0) new_len = 0;
    if (new_len > arr.arr.len) new_len = arr.arr.len;
    arr.arr.len = (int)new_len;
    return arr;
}

int hexa_len(HexaVal v) {
    if (v.tag == TAG_STR) return strlen(v.s);
    if (v.tag == TAG_ARRAY) return v.arr.len;
    if (v.tag == TAG_MAP) return v.map.len;
    return 0;
}

// ── Map operations (Optimization #10: hash table) ───────

// Allocate a new HexaMapTable with given hash-table capacity.
static HexaMapTable* hmap_alloc(int ht_cap) {
    HexaMapTable* t = (HexaMapTable*)calloc(1, sizeof(HexaMapTable));
    t->ht_cap = ht_cap;
    t->slots = (HexaMapSlot*)calloc(ht_cap, sizeof(HexaMapSlot));
    t->vals  = (HexaVal*)calloc(ht_cap, sizeof(HexaVal));
    int order_init = ht_cap < 8 ? 8 : ht_cap;
    t->order_keys = (char**)malloc(sizeof(char*) * order_init);
    t->order_vals = (HexaVal*)malloc(sizeof(HexaVal) * order_init);
    t->order_cap = order_init;
    t->len = 0;
    return t;
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
    free(old_slots);
    free(old_vals);
    t->slots = new_slots;
    t->vals  = new_vals;
    t->ht_cap = new_cap;
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
    v.map.tbl = NULL;
    v.map.len = 0;
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
    // Pre-size: (n+1) entries including __type__, target load < HMAP_LOAD_MAX/100.
    // Solve for smallest power-of-2 cap where (n+1)*100/cap < HMAP_LOAD_MAX.
    int need = (n + 1) * 100 / HMAP_LOAD_MAX + 1;
    int cap = HMAP_INIT_CAP;
    while (cap < need) cap <<= 1;
    HexaMapTable* t = hmap_alloc(cap);
    v.map.tbl = t;
    uint32_t mask = (uint32_t)(cap - 1);
    // Insert __type__ first (skip for anonymous map literals where
    // type_name is "" — they don't carry a __type__ field).
    if (type_name && type_name[0]) {
        uint32_t h = hexa_fnv1a_str("__type__");
        uint32_t idx = h & mask;
        while (t->slots[idx].key) idx = (idx + 1) & mask;
        t->slots[idx].key = strdup("__type__");
        t->slots[idx].hash = h;
        HexaVal tv = {.tag=TAG_STR};
        tv.s = strdup(type_name);
        t->vals[idx] = tv;
        t->order_keys[t->len] = t->slots[idx].key;
        t->order_vals[t->len] = tv;
        t->len++;
    }
    for (int i = 0; i < n; i++) {
        const char* k = keys[i];
        uint32_t h = hexa_fnv1a_str(k);
        uint32_t idx = h & mask;
        while (t->slots[idx].key) idx = (idx + 1) & mask;
        t->slots[idx].key = strdup(k);
        t->slots[idx].hash = h;
        t->vals[idx] = vals[i];
        t->order_keys[t->len] = t->slots[idx].key;
        t->order_vals[t->len] = vals[i];
        t->len++;
    }
    v.map.len = t->len;
    return v;
}

HexaVal hexa_map_set(HexaVal m, const char* key, HexaVal val) {
    if (_hx_stats_on()) _hx_stats_map_set++;
    // Lazy-alloc table on first insert
    if (!m.map.tbl) {
        m.map.tbl = hmap_alloc(HMAP_INIT_CAP);
    }
    HexaMapTable* t = m.map.tbl;
    uint32_t h = hexa_fnv1a_str(key);

    // Check if key exists (O(1) average)
    int si = hmap_find(t, key, h);
    if (si >= 0) {
        t->vals[si] = val;
        // Also update in the order array
        for (int i = 0; i < t->len; i++) {
            if (t->order_keys[i] == t->slots[si].key) {
                t->order_vals[i] = val;
                break;
            }
        }
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
    t->vals[idx] = val;

    // Append to insertion-order arrays
    if (t->len >= t->order_cap) {
        t->order_cap *= 2;
        t->order_keys = (char**)realloc(t->order_keys, sizeof(char*) * t->order_cap);
        t->order_vals = (HexaVal*)realloc(t->order_vals, sizeof(HexaVal) * t->order_cap);
    }
    t->order_keys[t->len] = t->slots[idx].key;  // shared pointer, not a copy
    t->order_vals[t->len] = val;
    t->len++;
    m.map.len = t->len;
    return m;
}

HexaVal hexa_map_get(HexaVal m, const char* key) {
    if (!m.map.tbl) {
        fprintf(stderr, "map key '%s' not found\n", key);
        return hexa_void();
    }
    HexaMapTable* t = m.map.tbl;
    uint32_t h = hexa_fnv1a_str(key);
    int si = hmap_find(t, key, h);
    if (si >= 0) return t->vals[si];
    fprintf(stderr, "map key '%s' not found\n", key);
    return hexa_void();
}

HexaVal hexa_map_keys(HexaVal m) {
    HexaVal arr = hexa_array_new();
    if (!m.map.tbl) return arr;
    HexaMapTable* t = m.map.tbl;
    arr = hexa_array_reserve(arr, t->len);
    for (int i = 0; i < t->len; i++) {
        arr = hexa_array_push(arr, hexa_str(t->order_keys[i]));
    }
    return arr;
}

HexaVal hexa_map_values(HexaVal m) {
    HexaVal arr = hexa_array_new();
    if (!m.map.tbl) return arr;
    HexaMapTable* t = m.map.tbl;
    arr = hexa_array_reserve(arr, t->len);
    for (int i = 0; i < t->len; i++) {
        arr = hexa_array_push(arr, t->order_vals[i]);
    }
    return arr;
}

int hexa_map_contains_key(HexaVal m, const char* key) {
    if (!m.map.tbl) return 0;
    uint32_t h = hexa_fnv1a_str(key);
    return hmap_find(m.map.tbl, key, h) >= 0;
}

HexaVal hexa_map_remove(HexaVal m, const char* key) {
    if (!m.map.tbl) return m;
    HexaMapTable* t = m.map.tbl;
    uint32_t h = hexa_fnv1a_str(key);
    int si = hmap_find(t, key, h);
    if (si < 0) return m;

    // Remove from insertion-order arrays
    char* removed_key = t->slots[si].key;
    for (int i = 0; i < t->len; i++) {
        if (t->order_keys[i] == removed_key) {
            for (int j = i; j < t->len - 1; j++) {
                t->order_keys[j] = t->order_keys[j+1];
                t->order_vals[j] = t->order_vals[j+1];
            }
            break;
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
        t->slots[ri] = saved_slot;
        t->vals[ri] = saved_val;
        // Update order_keys pointer if it moved
        for (int k = 0; k < t->len; k++) {
            if (t->order_keys[k] == saved_slot.key) {
                // pointer unchanged, just slot index moved -- no action needed
                break;
            }
        }
        ci = (ci + 1) & mask;
    }

    t->len--;
    m.map.len = t->len;
    return m;
}

// Polymorphic index get: array[int] or map[string]
HexaVal hexa_index_get(HexaVal container, HexaVal key) {
    if (container.tag == TAG_MAP && key.tag == TAG_STR) {
        return hexa_map_get(container, key.s);
    }
    return hexa_array_get(container, key.i);
}

// For-in dispatch: array -> element, map -> key as string.
// Uses insertion-order arrays so maps iterate in insertion order (Python-style).
HexaVal hexa_iter_get(HexaVal v, int64_t idx) {
    if (v.tag == TAG_MAP) {
        if (!v.map.tbl || idx < 0 || idx >= v.map.len) {
            fprintf(stderr, "map iter index %lld out of bounds (len %d)\n", (long long)idx, v.map.len);
            exit(1);
        }
        return hexa_str(v.map.tbl->order_keys[idx]);
    }
    return hexa_array_get(v, idx);
}

// Polymorphic index set: array[int]=val or map[string]=val
HexaVal hexa_index_set(HexaVal container, HexaVal key, HexaVal val) {
    if (container.tag == TAG_MAP && key.tag == TAG_STR) {
        return hexa_map_set(container, key.s, val);
    }
    return hexa_array_set(container, key.i, val);
}

// Silent type check for struct method dispatch (codegen_c2 ImplBlock support).
// Returns 1 if v is a TAG_MAP carrying a "__type__" field equal to type_name.
// Uses hash lookup instead of linear scan.
int hexa_is_type(HexaVal v, const char* type_name) {
    if (v.tag != TAG_MAP || !v.map.tbl) return 0;
    uint32_t h = hexa_fnv1a_str("__type__");
    int si = hmap_find(v.map.tbl, "__type__", h);
    if (si < 0) return 0;
    HexaVal t = v.map.tbl->vals[si];
    return t.tag == TAG_STR && t.s && strcmp(t.s, type_name) == 0;
}

// ── String operations ────────────────────────────────────

HexaVal hexa_str_concat(HexaVal a, HexaVal b) {
    if (_hx_stats_on()) _hx_stats_str_concat++;
    char* sa = a.tag == TAG_STR ? a.s : "";
    char* sb = b.tag == TAG_STR ? b.s : "";
    char* result = malloc(strlen(sa) + strlen(sb) + 1);
    strcpy(result, sa); strcat(result, sb);
    return hexa_str_own(result);
}

HexaVal hexa_str_chars(HexaVal s) {
    HexaVal arr = hexa_array_new();
    if (s.tag != TAG_STR) return arr;
    for (int i = 0; s.s[i]; i++) {
        char buf[2] = {s.s[i], 0};
        arr = hexa_array_push(arr, hexa_str(buf));
    }
    return arr;
}

int hexa_str_contains(HexaVal s, HexaVal sub) {
    return strstr(s.s, sub.s) != NULL;
}

int hexa_str_eq(HexaVal a, HexaVal b) {
    if (a.tag != TAG_STR || b.tag != TAG_STR) return 0;
    // Optimization #11: interned strings share pointers
    if (a.s == b.s) return 1;
    return strcmp(a.s, b.s) == 0;
}

int hexa_str_starts_with(HexaVal s, HexaVal prefix) {
    if (s.tag != TAG_STR || prefix.tag != TAG_STR) return 0;
    size_t plen = strlen(prefix.s);
    return strncmp(s.s, prefix.s, plen) == 0;
}

int hexa_str_ends_with(HexaVal s, HexaVal suffix) {
    if (s.tag != TAG_STR || suffix.tag != TAG_STR) return 0;
    size_t slen = strlen(s.s);
    size_t sfxlen = strlen(suffix.s);
    if (sfxlen > slen) return 0;
    return strcmp(s.s + slen - sfxlen, suffix.s) == 0;
}

HexaVal hexa_str_substring(HexaVal s, HexaVal start, HexaVal end) {
    if (s.tag != TAG_STR) return hexa_str("");
    int64_t slen = strlen(s.s);
    int64_t a = start.i, b = end.i;
    if (a < 0) a = 0;
    if (b > slen) b = slen;
    if (a >= b) return hexa_str("");
    char* buf = (char*)malloc(b - a + 1);
    memcpy(buf, s.s + a, b - a);
    buf[b - a] = '\0';
    return hexa_str_own(buf);
}

int64_t hexa_str_index_of(HexaVal s, HexaVal sub) {
    if (s.tag != TAG_STR || sub.tag != TAG_STR) return -1;
    char* p = strstr(s.s, sub.s);
    if (!p) return -1;
    return (int64_t)(p - s.s);
}

// ── Array operations ─────────────────────────────────
HexaVal hexa_array_pop(HexaVal arr) {
    if (arr.tag != TAG_ARRAY || arr.arr.len == 0) return hexa_void();
    return arr.arr.items[arr.arr.len - 1];
}

HexaVal hexa_array_reverse(HexaVal arr) {
    if (arr.tag != TAG_ARRAY) return arr;
    HexaVal result = hexa_array_new();
    for (int i = arr.arr.len - 1; i >= 0; i--)
        result = hexa_array_push(result, arr.arr.items[i]);
    return result;
}

static int hexa_sort_cmp(const void* a, const void* b) {
    HexaVal va = *(const HexaVal*)a, vb = *(const HexaVal*)b;
    if (va.tag == TAG_INT && vb.tag == TAG_INT) return va.i < vb.i ? -1 : va.i > vb.i ? 1 : 0;
    if (va.tag == TAG_FLOAT && vb.tag == TAG_FLOAT) return va.f < vb.f ? -1 : va.f > vb.f ? 1 : 0;
    return 0;
}

HexaVal hexa_array_sort(HexaVal arr) {
    if (arr.tag != TAG_ARRAY) return arr;
    HexaVal result = arr;
    result.arr.items = (HexaVal*)malloc(sizeof(HexaVal) * arr.arr.len);
    memcpy(result.arr.items, arr.arr.items, sizeof(HexaVal) * arr.arr.len);
    qsort(result.arr.items, result.arr.len, sizeof(HexaVal), hexa_sort_cmp);
    return result;
}

// ── Exec ────────────────────────────────────────────
HexaVal hexa_exec(HexaVal cmd) {
    if (cmd.tag != TAG_STR) return hexa_str("");
    FILE* fp = popen(cmd.s, "r");
    if (!fp) return hexa_str("");
    char buf[4096]; size_t total = 0;
    char* result = (char*)malloc(4096); result[0] = '\0';
    size_t cap = 4096;
    while (fgets(buf, sizeof(buf), fp)) {
        size_t len = strlen(buf);
        if (total + len >= cap) { cap *= 2; result = (char*)realloc(result, cap); }
        strcat(result, buf);
        total += len;
    }
    pclose(fp);
    return hexa_str_own(result);
}

// ── Stderr ──────────────────────────────────────────
void hexa_eprint_val(HexaVal v) {
    if (v.tag == TAG_STR) fprintf(stderr, "%s", v.s);
    else if (v.tag == TAG_INT) fprintf(stderr, "%lld", (long long)v.i);
    else if (v.tag == TAG_FLOAT) fprintf(stderr, "%g", v.f);
    else if (v.tag == TAG_BOOL) fprintf(stderr, v.b ? "true" : "false");
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

void hexa_throw(HexaVal err) {
    __hexa_error_val = err;
    if (__hexa_try_top > 0) {
        __hexa_try_top--;
        longjmp(*__hexa_try_stack[__hexa_try_top], 1);
    }
    fprintf(stderr, "Unhandled throw\n");
    exit(1);
}

// ── Printing ─────────────────────────────────────────────

void hexa_print_val(HexaVal v) {
    switch (v.tag) {
        case TAG_INT: printf("%lld", (long long)v.i); break;
        case TAG_FLOAT: printf("%g", v.f); break;
        case TAG_BOOL: printf("%s", v.b ? "true" : "false"); break;
        case TAG_STR: printf("%s", v.s); break;
        case TAG_VOID: printf("void"); break;
        case TAG_ARRAY:
            printf("[");
            for (int i = 0; i < v.arr.len; i++) {
                if (i > 0) printf(", ");
                hexa_print_val(v.arr.items[i]);
            }
            printf("]");
            break;
        default: printf("<value>"); break;
    }
}

void hexa_println(HexaVal v) { hexa_print_val(v); printf("\n"); }

// ── to_string ────────────────────────────────────────────

HexaVal hexa_to_string(HexaVal v) {
    char buf[64];
    switch (v.tag) {
        case TAG_INT: snprintf(buf, 64, "%lld", (long long)v.i); return hexa_str(buf);
        case TAG_FLOAT: snprintf(buf, 64, "%g", v.f); return hexa_str(buf);
        case TAG_BOOL: return hexa_str(v.b ? "true" : "false");
        case TAG_STR: return v;
        case TAG_VOID: return hexa_str("void");
        default: return hexa_str("<value>");
    }
}

// ── Truthy check ─────────────────────────────────────────

int hexa_truthy(HexaVal v) {
    switch (v.tag) {
        case TAG_BOOL: return v.b;
        case TAG_INT: return v.i != 0;
        case TAG_STR: return v.s[0] != 0;
        case TAG_VOID: return 0;
        default: return 1;
    }
}

// ── Type checking ────────────────────────────────────────

HexaVal hexa_type_of(HexaVal v) {
    switch (v.tag) {
        case TAG_INT: return hexa_str("int");
        case TAG_FLOAT: return hexa_str("float");
        case TAG_BOOL: return hexa_str("bool");
        case TAG_STR: return hexa_str("string");
        case TAG_VOID: return hexa_str("void");
        case TAG_ARRAY: return hexa_str("array");
        case TAG_MAP: return hexa_str("map");
        default: return hexa_str("unknown");
    }
}

// ── Polymorphic add (int + or string concat) ─────────────

HexaVal hexa_add(HexaVal a, HexaVal b) {
    if (a.tag == TAG_INT && b.tag == TAG_INT) return hexa_int(a.i + b.i);
    if (a.tag == TAG_FLOAT || b.tag == TAG_FLOAT) {
        double fa = a.tag == TAG_FLOAT ? a.f : (double)a.i;
        double fb = b.tag == TAG_FLOAT ? b.f : (double)b.i;
        return hexa_float(fa + fb);
    }
    HexaVal sa = hexa_to_string(a);
    HexaVal sb = hexa_to_string(b);
    return hexa_str_concat(sa, sb);
}

// ── Polymorphic equality ─────────────────────────────────

HexaVal hexa_eq(HexaVal a, HexaVal b) {
    if (a.tag != b.tag) return hexa_bool(0);
    switch (a.tag) {
        case TAG_INT: return hexa_bool(a.i == b.i);
        case TAG_FLOAT: return hexa_bool(a.f == b.f);
        case TAG_BOOL: return hexa_bool(a.b == b.b);
        case TAG_STR: return hexa_bool(a.s == b.s || strcmp(a.s, b.s) == 0);
        case TAG_VOID: return hexa_bool(1);
        default: return hexa_bool(0);
    }
}

// ── File I/O ─────────────────────────────────────────────

HexaVal hexa_read_file(HexaVal path) {
    FILE* f = fopen(path.s, "r");
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

HexaVal hexa_write_file(HexaVal path, HexaVal content) {
    FILE* f = fopen(path.s, "w");
    if (!f) return hexa_bool(0);
    fputs(content.s, f);
    fclose(f);
    return hexa_bool(1);
}

// ── Command line arguments ───────────────────────────

static int _hexa_argc = 0;
static char** _hexa_argv = NULL;

void hexa_set_args(int argc, char** argv) {
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

// ── Float operations ─────────────────────────────────

#include <math.h>

HexaVal hexa_sqrt(HexaVal v) {
    double x = v.tag == TAG_FLOAT ? v.f : (double)v.i;
    return hexa_float(sqrt(x));
}

HexaVal hexa_pow(HexaVal base, HexaVal exp) {
    double b = base.tag == TAG_FLOAT ? base.f : (double)base.i;
    double e = exp.tag == TAG_FLOAT ? exp.f : (double)exp.i;
    return hexa_float(pow(b, e));
}

HexaVal hexa_floor(HexaVal v) {
    return hexa_int((int64_t)floor(v.tag == TAG_FLOAT ? v.f : (double)v.i));
}

HexaVal hexa_ceil(HexaVal v) {
    return hexa_int((int64_t)ceil(v.tag == TAG_FLOAT ? v.f : (double)v.i));
}

HexaVal hexa_abs(HexaVal v) {
    if (v.tag == TAG_INT) return hexa_int(v.i < 0 ? -v.i : v.i);
    return hexa_float(fabs(v.f));
}

// ── String format ────────────────────────────────────

// Multi-arg format: hexa_format_n(fmt, args_array)
HexaVal hexa_format(HexaVal fmt, HexaVal arg) {
    // Single arg: replace first {} with arg
    if (fmt.tag != TAG_STR) return fmt;
    char* pos = strstr(fmt.s, "{}");
    if (!pos) return fmt;
    HexaVal sarg = hexa_to_string(arg);
    int before = pos - fmt.s;
    int after_len = strlen(pos + 2);
    char* result = malloc(before + strlen(sarg.s) + after_len + 1);
    strncpy(result, fmt.s, before);
    result[before] = 0;
    strcat(result, sarg.s);
    strcat(result, pos + 2);
    return hexa_str_own(result);
}

HexaVal hexa_format_n(HexaVal fmt, HexaVal args) {
    // Multi-arg: replace {} and {:.N} with successive args
    if (fmt.tag != TAG_STR || args.tag != TAG_ARRAY) return fmt;
    char* result = malloc(strlen(fmt.s) * 4 + args.arr.len * 64 + 256);
    result[0] = 0;
    char* src = fmt.s;
    int ai = 0;
    while (*src) {
        if (src[0] == '{' && ai < args.arr.len) {
            // Find closing }
            char* close = strchr(src, '}');
            if (close) {
                int speclen = (int)(close - src - 1);
                char spec[32] = {0};
                if (speclen > 0 && speclen < 31) memcpy(spec, src+1, speclen);
                char buf[128];
                HexaVal arg = args.arr.items[ai++];
                if (spec[0] == ':' && spec[1] == '.') {
                    // Precision format {:.N}
                    int prec = atoi(spec + 2);
                    double val = (arg.tag == TAG_FLOAT) ? arg.f : (double)arg.i;
                    snprintf(buf, sizeof(buf), "%.*f", prec, val);
                } else if (spec[0] == 0) {
                    // Simple {}
                    HexaVal s = hexa_to_string(arg);
                    strncpy(buf, s.s, sizeof(buf)-1); buf[sizeof(buf)-1]=0;
                } else {
                    HexaVal s = hexa_to_string(arg);
                    strncpy(buf, s.s, sizeof(buf)-1); buf[sizeof(buf)-1]=0;
                }
                strcat(result, buf);
                src = close + 1;
            } else {
                int len = strlen(result);
                result[len] = *src; result[len+1] = 0;
                src++;
            }
        } else {
            int len = strlen(result);
            result[len] = *src; result[len+1] = 0;
            src++;
        }
    }
    return hexa_str_own(result);
}

// ── String split ─────────────────────────────────────

HexaVal hexa_str_split(HexaVal s, HexaVal delim) {
    HexaVal arr = hexa_array_new();
    if (s.tag != TAG_STR || delim.tag != TAG_STR) return arr;
    char* src = strdup(s.s);
    char* d = delim.s;
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

HexaVal hexa_str_trim(HexaVal s) {
    if (s.tag != TAG_STR) return s;
    char* str = s.s;
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;
    int len = strlen(str);
    while (len > 0 && (str[len-1] == ' ' || str[len-1] == '\t' || str[len-1] == '\n' || str[len-1] == '\r')) len--;
    char* result = strndup(str, len);
    return hexa_str_own(result);
}

HexaVal hexa_str_replace(HexaVal s, HexaVal old, HexaVal new_s) {
    if (s.tag != TAG_STR) return s;
    char* result = malloc(strlen(s.s) * 2 + 1);
    result[0] = 0;
    char* pos = s.s;
    int oldlen = strlen(old.s);
    while (1) {
        char* found = strstr(pos, old.s);
        if (!found) { strcat(result, pos); break; }
        strncat(result, pos, found - pos);
        strcat(result, new_s.s);
        pos = found + oldlen;
    }
    return hexa_str_own(result);
}

HexaVal hexa_str_to_upper(HexaVal s) {
    if (s.tag != TAG_STR) return s;
    char* r = strdup(s.s);
    for (int i = 0; r[i]; i++) if (r[i] >= 'a' && r[i] <= 'z') r[i] -= 32;
    return hexa_str_own(r);
}

HexaVal hexa_str_to_lower(HexaVal s) {
    if (s.tag != TAG_STR) return s;
    char* r = strdup(s.s);
    for (int i = 0; r[i]; i++) if (r[i] >= 'A' && r[i] <= 'Z') r[i] += 32;
    return hexa_str_own(r);
}

HexaVal hexa_str_join(HexaVal arr, HexaVal sep) {
    if (arr.tag != TAG_ARRAY || arr.arr.len == 0) return hexa_str("");
    int total = 0;
    for (int i = 0; i < arr.arr.len; i++) {
        HexaVal s = hexa_to_string(arr.arr.items[i]);
        total += strlen(s.s);
    }
    total += (arr.arr.len - 1) * strlen(sep.s);
    char* result = malloc(total + 1);
    result[0] = 0;
    for (int i = 0; i < arr.arr.len; i++) {
        if (i > 0) strcat(result, sep.s);
        HexaVal s = hexa_to_string(arr.arr.items[i]);
        strcat(result, s.s);
    }
    return hexa_str_own(result);
}
static int utf8_cpcount(const char* s) {
    int n = 0;
    for (int i = 0; s[i]; i++) if ((s[i] & 0xC0) != 0x80) n++;
    return n;
}
HexaVal hexa_pad_left(HexaVal s, HexaVal width) {
    HexaVal str = hexa_to_string(s);
    int w = width.i;
    int cplen = utf8_cpcount(str.s);
    int bytelen = strlen(str.s);
    if (cplen >= w) return str;
    int pad = w - cplen;
    char* result = malloc(bytelen + pad + 1);
    memset(result, ' ', pad);
    strcpy(result + pad, str.s);
    return hexa_str_own(result);
}


HexaVal hexa_pad_right(HexaVal s, HexaVal width) {
    HexaVal str = hexa_to_string(s);
    int w = width.i;
    int cplen = utf8_cpcount(str.s);
    int bytelen = strlen(str.s);
    if (cplen >= w) return str;
    int pad = w - cplen;
    char* result = malloc(bytelen + pad + 1);
    strcpy(result, str.s);
    memset(result + bytelen, ' ', pad);
    result[bytelen + pad] = 0;
    return hexa_str_own(result);
}

// B-19: Polymorphic arithmetic
HexaVal hexa_sub(HexaVal a, HexaVal b) {
    if (a.tag == TAG_INT && b.tag == TAG_INT) return hexa_int(a.i - b.i);
    double fa = a.tag == TAG_FLOAT ? a.f : (double)a.i;
    double fb = b.tag == TAG_FLOAT ? b.f : (double)b.i;
    return hexa_float(fa - fb);
}
HexaVal hexa_mul(HexaVal a, HexaVal b) {
    if (a.tag == TAG_INT && b.tag == TAG_INT) return hexa_int(a.i * b.i);
    double fa = a.tag == TAG_FLOAT ? a.f : (double)a.i;
    double fb = b.tag == TAG_FLOAT ? b.f : (double)b.i;
    return hexa_float(fa * fb);
}
HexaVal hexa_div(HexaVal a, HexaVal b) {
    if (a.tag == TAG_INT && b.tag == TAG_INT) {
        if (b.i == 0) return hexa_int(0);
        return hexa_int(a.i / b.i);
    }
    double fa = a.tag == TAG_FLOAT ? a.f : (double)a.i;
    double fb = b.tag == TAG_FLOAT ? b.f : (double)b.i;
    return hexa_float(fa / fb);
}
HexaVal hexa_mod(HexaVal a, HexaVal b) {
    if (a.tag == TAG_INT && b.tag == TAG_INT) return hexa_int(b.i ? a.i % b.i : 0);
    return hexa_int(0);
}
HexaVal hexa_str_repeat(HexaVal s, HexaVal n) {
    if (s.tag != TAG_STR) return s;
    int count = n.i;
    int slen = strlen(s.s);
    char* result = malloc(slen * count + 1);
    result[0] = 0;
    for (int i = 0; i < count; i++) strcat(result, s.s);
    return hexa_str_own(result);
}

int hexa_array_contains(HexaVal arr, HexaVal item) {
    if (arr.tag != TAG_ARRAY) return 0;
    for (int i = 0; i < arr.arr.len; i++) {
        if (hexa_truthy(hexa_eq(arr.arr.items[i], item))) return 1;
    }
    return 0;
}

HexaVal hexa_format_float(HexaVal f, HexaVal prec) {
    double v = f.tag == TAG_FLOAT ? f.f : (double)f.i;
    int p = prec.i;
    char buf[64];
    snprintf(buf, 64, "%.*f", p, v);
    return hexa_str(buf);
}

HexaVal hexa_format_float_sci(HexaVal f, HexaVal prec) {
    double v = f.tag == TAG_FLOAT ? f.f : (double)f.i;
    int p = prec.i;
    char buf[64];
    snprintf(buf, 64, "%.*e", p, v);
    return hexa_str(buf);
}

// ── Extern FFI: dlopen / dlsym / dispatch ───────────────

// Resolve a library path for dlopen.
// If lib_name is NULL or empty, use RTLD_DEFAULT (search default symbols).
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
    switch (v.tag) {
        case TAG_INT:   return v.i;
        case TAG_FLOAT: { union { double d; int64_t i; } u; u.d = v.f; return u.i; }
        case TAG_BOOL:  return (int64_t)v.b;
        case TAG_STR:   return (int64_t)(uintptr_t)v.s;
        case TAG_VOID:  return 0;
        default:        return 0;
    }
}

// Raw extern call dispatch — supports 0 to 12 arguments.
// Extended 2026-04-12 from 10 to 12 to support cuLaunchKernel (11 args).
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
        default:
            fprintf(stderr, "[hexa-ffi] extern call with %d args not supported (max 12)\n", nargs);
            return 0;
    }
}

// High-level extern call: resolve, marshal, call, unmarshal.
// ret_kind: 0=void, 1=int, 2=float, 3=bool, 4=pointer/string
HexaVal hexa_extern_call(void* fn_ptr, HexaVal* hargs, int nargs, int ret_kind) {
    int64_t cargs[12];  // was 10; extended for cuLaunchKernel (11 args)
    for (int i = 0; i < nargs && i < 12; i++) {
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
// Supports up to 8 args (covers all common FFI patterns including NSRect).
// The C compiler sees typed args and emits correct register moves.
HexaVal hexa_extern_call_typed(void* fn_ptr, HexaVal* hargs, int nargs, int ret_kind, int float_mask) {
    // Extract values: doubles from .f, integers from .i
    double  fv[8]; // float values
    int64_t iv[8]; // int values
    for (int i = 0; i < nargs && i < 8; i++) {
        if (float_mask & (1 << i)) {
            fv[i] = (hargs[i].tag == TAG_FLOAT) ? hargs[i].f : (double)hargs[i].i;
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
        default: {
            // For 3+ args: fall back to the codegen approach (typed wrappers).
            // This runtime path handles the most common 0-2 arg float cases.
            // For higher arities, codegen_c2 generates typed C wrapper functions.
            fprintf(stderr, "[hexa-ffi] typed call with %d args: use typed extern fn decl in .hexa\n", nargs);
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
    if (s.tag != TAG_STR) return hexa_int(0);
    return hexa_int((int64_t)(uintptr_t)s.s);
}

HexaVal hexa_from_cstring(HexaVal ptr) {
    int64_t p = (ptr.tag == TAG_INT) ? ptr.i : 0;
    if (p == 0) return hexa_str("");
    return hexa_str((const char*)(uintptr_t)p);
}

HexaVal hexa_ptr_null() { return hexa_int(0); }

HexaVal hexa_ptr_addr(HexaVal v) {
    return hexa_int(v.i);
}

// ── G5: Pointer arithmetic builtins ─────────────────────

HexaVal hexa_ptr_alloc(HexaVal size) {
    int64_t n = size.tag == TAG_INT ? size.i : 0;
    if (n <= 0) return hexa_int(0);
    void* p = calloc(1, (size_t)n);
    return hexa_int((int64_t)(uintptr_t)p);
}

HexaVal hexa_ptr_free(HexaVal ptr, HexaVal size) {
    int64_t p = (ptr.tag == TAG_INT) ? ptr.i : 0;
    if (p != 0) free((void*)(uintptr_t)p);
    return hexa_void();
}

HexaVal hexa_ptr_write(HexaVal ptr, HexaVal offset, HexaVal val) {
    int64_t p = (ptr.tag == TAG_INT) ? ptr.i : 0;
    int64_t off = (offset.tag == TAG_INT) ? offset.i : 0;
    if (p == 0) return hexa_void();
    uint8_t* base = (uint8_t*)(uintptr_t)p + off;
    if (val.tag == TAG_FLOAT) {
        double d = val.f;
        memcpy(base, &d, sizeof(double));
    } else {
        int64_t v = val.i;
        memcpy(base, &v, sizeof(int64_t));
    }
    return hexa_void();
}

HexaVal hexa_ptr_write_f32(HexaVal ptr, HexaVal offset, HexaVal val) {
    int64_t p = (ptr.tag == TAG_INT) ? ptr.i : 0;
    int64_t off = (offset.tag == TAG_INT) ? offset.i : 0;
    if (p == 0) return hexa_void();
    float f = (float)(val.tag == TAG_FLOAT ? val.f : (double)val.i);
    memcpy((uint8_t*)(uintptr_t)p + off, &f, sizeof(float));
    return hexa_void();
}

HexaVal hexa_ptr_write_i32(HexaVal ptr, HexaVal offset, HexaVal val) {
    int64_t p = (ptr.tag == TAG_INT) ? ptr.i : 0;
    int64_t off = (offset.tag == TAG_INT) ? offset.i : 0;
    if (p == 0) return hexa_void();
    int32_t v = (int32_t)val.i;
    memcpy((uint8_t*)(uintptr_t)p + off, &v, sizeof(int32_t));
    return hexa_void();
}

HexaVal hexa_ptr_read(HexaVal ptr, HexaVal offset) {
    int64_t p = (ptr.tag == TAG_INT) ? ptr.i : 0;
    int64_t off = (offset.tag == TAG_INT) ? offset.i : 0;
    if (p == 0) return hexa_int(0);
    int64_t v;
    memcpy(&v, (uint8_t*)(uintptr_t)p + off, sizeof(int64_t));
    return hexa_int(v);
}

HexaVal hexa_ptr_read_f64(HexaVal ptr, HexaVal offset) {
    int64_t p = (ptr.tag == TAG_INT) ? ptr.i : 0;
    int64_t off = (offset.tag == TAG_INT) ? offset.i : 0;
    if (p == 0) return hexa_float(0.0);
    double d;
    memcpy(&d, (uint8_t*)(uintptr_t)p + off, sizeof(double));
    return hexa_float(d);
}

HexaVal hexa_ptr_read_f32(HexaVal ptr, HexaVal offset) {
    int64_t p = (ptr.tag == TAG_INT) ? ptr.i : 0;
    int64_t off = (offset.tag == TAG_INT) ? offset.i : 0;
    if (p == 0) return hexa_float(0.0);
    float f;
    memcpy(&f, (uint8_t*)(uintptr_t)p + off, sizeof(float));
    return hexa_float((double)f);
}

HexaVal hexa_ptr_read_i32(HexaVal ptr, HexaVal offset) {
    int64_t p = (ptr.tag == TAG_INT) ? ptr.i : 0;
    int64_t off = (offset.tag == TAG_INT) ? offset.i : 0;
    if (p == 0) return hexa_int(0);
    int32_t v;
    memcpy(&v, (uint8_t*)(uintptr_t)p + off, sizeof(int32_t));
    return hexa_int((int64_t)v);
}

HexaVal hexa_ptr_offset(HexaVal ptr, HexaVal offset) {
    int64_t p = (ptr.tag == TAG_INT) ? ptr.i : 0;
    int64_t off = (offset.tag == TAG_INT) ? offset.i : 0;
    return hexa_int(p + off);
}

HexaVal hexa_deref(HexaVal ptr) {
    int64_t p = (ptr.tag == TAG_INT) ? ptr.i : 0;
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
        buf[i] = (args[i].tag == TAG_FLOAT) ? args[i].f : (double)args[i].i;
    }
    return hexa_int((int64_t)(uintptr_t)buf);
}

// Pack N f32 values into a newly allocated buffer. Returns pointer as int.
HexaVal hexa_struct_pack_f32(HexaVal* args, int nargs) {
    if (nargs <= 0) return hexa_int(0);
    size_t total = (size_t)nargs * sizeof(float);
    float* buf = (float*)calloc(1, total);
    for (int i = 0; i < nargs; i++) {
        buf[i] = (float)((args[i].tag == TAG_FLOAT) ? args[i].f : (double)args[i].i);
    }
    return hexa_int((int64_t)(uintptr_t)buf);
}

// Read the Nth f64 from a struct pointer.
HexaVal hexa_struct_unpack(HexaVal ptr, HexaVal index) {
    int64_t p = (ptr.tag == TAG_INT) ? ptr.i : 0;
    int64_t idx = (index.tag == TAG_INT) ? index.i : 0;
    if (p == 0) return hexa_float(0.0);
    double* buf = (double*)(uintptr_t)p;
    return hexa_float(buf[idx]);
}

// Read the Nth f32 from a struct pointer.
HexaVal hexa_struct_unpack_f32(HexaVal ptr, HexaVal index) {
    int64_t p = (ptr.tag == TAG_INT) ? ptr.i : 0;
    int64_t idx = (index.tag == TAG_INT) ? index.i : 0;
    if (p == 0) return hexa_float(0.0);
    float* buf = (float*)(uintptr_t)p;
    return hexa_float((double)buf[idx]);
}

// Convenience: pack an NSRect (4x f64 = 32 bytes).
HexaVal hexa_struct_rect(HexaVal x, HexaVal y, HexaVal w, HexaVal h) {
    double* buf = (double*)calloc(1, 4 * sizeof(double));
    buf[0] = (x.tag == TAG_FLOAT) ? x.f : (double)x.i;
    buf[1] = (y.tag == TAG_FLOAT) ? y.f : (double)y.i;
    buf[2] = (w.tag == TAG_FLOAT) ? w.f : (double)w.i;
    buf[3] = (h.tag == TAG_FLOAT) ? h.f : (double)h.i;
    return hexa_int((int64_t)(uintptr_t)buf);
}

// Convenience: pack an NSPoint (2x f64 = 16 bytes).
HexaVal hexa_struct_point(HexaVal x, HexaVal y) {
    double* buf = (double*)calloc(1, 2 * sizeof(double));
    buf[0] = (x.tag == TAG_FLOAT) ? x.f : (double)x.i;
    buf[1] = (y.tag == TAG_FLOAT) ? y.f : (double)y.i;
    return hexa_int((int64_t)(uintptr_t)buf);
}

// Convenience: pack an NSSize (2x f64 = 16 bytes).
HexaVal hexa_struct_size_pack(HexaVal w, HexaVal h) {
    double* buf = (double*)calloc(1, 2 * sizeof(double));
    buf[0] = (w.tag == TAG_FLOAT) ? w.f : (double)w.i;
    buf[1] = (h.tag == TAG_FLOAT) ? h.f : (double)h.i;
    return hexa_int((int64_t)(uintptr_t)buf);
}

// Free a struct pointer (allocated by struct_pack/struct_rect/etc).
HexaVal hexa_struct_free(HexaVal ptr) {
    int64_t p = (ptr.tag == TAG_INT) ? ptr.i : 0;
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

void hexa_set_callback_dispatcher(hexa_cb_dispatch_fn fn) {
    __hexa_cb_dispatcher = fn;
}

// Internal dispatch: called by every trampoline.
// Converts raw C int64 args back to HexaVal and calls the registered function.
static int64_t hexa_trampoline_dispatch(int slot_id, int argc, int64_t* args) {
    if (slot_id < 0 || slot_id >= HEXA_TRAMPOLINE_POOL_SIZE) return 0;
    if (!__hexa_cb_slots[slot_id].in_use) return 0;

    // If a custom dispatcher is set (e.g. interpreter), use it
    if (__hexa_cb_dispatcher) {
        HexaVal result = __hexa_cb_dispatcher(slot_id, argc, args);
        if (result.tag == TAG_INT) return result.i;
        if (result.tag == TAG_FLOAT) { union { double d; int64_t i; } u; u.d = result.f; return u.i; }
        return 0;
    }

    // For compiled code: call the fn_ptr stored in the slot directly.
    // Compiled hexa functions take HexaVal args and return HexaVal.
    // We wrap each raw int64 arg as hexa_int() before calling.
    HexaCallbackSlot* slot = &__hexa_cb_slots[slot_id];
    if (slot->hexa_fn.tag == TAG_FN && slot->hexa_fn.fn.fn_ptr) {
        typedef HexaVal (*HFn0)(void);
        typedef HexaVal (*HFn1)(HexaVal);
        typedef HexaVal (*HFn2)(HexaVal, HexaVal);
        typedef HexaVal (*HFn3)(HexaVal, HexaVal, HexaVal);
        typedef HexaVal (*HFn4)(HexaVal, HexaVal, HexaVal, HexaVal);
        void* fp = slot->hexa_fn.fn.fn_ptr;
        HexaVal result;
        switch (argc) {
            case 0: result = ((HFn0)fp)(); break;
            case 1: result = ((HFn1)fp)(hexa_int(args[0])); break;
            case 2: result = ((HFn2)fp)(hexa_int(args[0]), hexa_int(args[1])); break;
            case 3: result = ((HFn3)fp)(hexa_int(args[0]), hexa_int(args[1]), hexa_int(args[2])); break;
            default: result = ((HFn4)fp)(hexa_int(args[0]), hexa_int(args[1]), hexa_int(args[2]), hexa_int(args[3])); break;
        }
        if (result.tag == TAG_INT) return result.i;
        if (result.tag == TAG_FLOAT) { union { double d; int64_t i; } u; u.d = result.f; return u.i; }
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
    int64_t p = (ptr.tag == TAG_INT) ? ptr.i : 0;
    for (int i = 0; i < HEXA_TRAMPOLINE_POOL_SIZE; i++) {
        if ((int64_t)(uintptr_t)__hexa_cb_slots[i].fn_ptr == p) {
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
    int64_t p = (ptr.tag == TAG_INT) ? ptr.i : 0;
    for (int i = 0; i < HEXA_TRAMPOLINE_POOL_SIZE; i++) {
        if ((int64_t)(uintptr_t)__hexa_cb_slots[i].fn_ptr == p) {
            return hexa_int(i);
        }
    }
    return hexa_int(-1);
}

// callback_get_fn(slot_id) -> HexaVal
// Returns the hexa function registered at the given slot.
HexaVal hexa_callback_get_fn(int slot_id) {
    if (slot_id < 0 || slot_id >= HEXA_TRAMPOLINE_POOL_SIZE) return hexa_void();
    if (!__hexa_cb_slots[slot_id].in_use) return hexa_void();
    return __hexa_cb_slots[slot_id].hexa_fn;
}

// ===== B4: tensor stubs + clock/random =====
typedef struct { int64_t rows; int64_t cols; float* data; } HexaTensorStub;

HexaVal hexa_tensor_new(HexaVal r, HexaVal c) {
    int64_t rows = (r.tag==TAG_INT)?r.i:(int64_t)r.f;
    int64_t cols = (c.tag==TAG_INT)?c.i:(int64_t)c.f;
    HexaTensorStub* t = (HexaTensorStub*)calloc(1, sizeof(*t));
    t->rows = rows; t->cols = cols;
    t->data = (float*)calloc((size_t)(rows*cols), sizeof(float));
    return hexa_int((int64_t)(uintptr_t)t);
}

HexaVal hexa_tensor_randn(HexaVal r, HexaVal c) {
    HexaVal v = hexa_tensor_new(r, c);
    HexaTensorStub* t = (HexaTensorStub*)(uintptr_t)v.i;
    int64_t n = t->rows * t->cols;
    for (int64_t i = 0; i < n; i++) {
        double u = (rand()+1.0)/(RAND_MAX+1.0);
        double w = (rand()+1.0)/(RAND_MAX+1.0);
        t->data[i] = (float)(sqrt(-2.0*log(u)) * cos(6.28318530718*w));
    }
    return v;
}

HexaVal hexa_tensor_data_ptr(HexaVal tv) {
    HexaTensorStub* t = (HexaTensorStub*)(uintptr_t)tv.i;
    return hexa_int((int64_t)(uintptr_t)t->data);
}

HexaVal hexa_tensor_from_ptr(HexaVal p, HexaVal r, HexaVal c) {
    HexaTensorStub* t = (HexaTensorStub*)calloc(1, sizeof(*t));
    t->rows = (r.tag==TAG_INT)?r.i:(int64_t)r.f;
    t->cols = (c.tag==TAG_INT)?c.i:(int64_t)c.f;
    t->data = (float*)(uintptr_t)((p.tag==TAG_INT)?p.i:(int64_t)p.f);
    return hexa_int((int64_t)(uintptr_t)t);
}

HexaVal hexa_clock(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return hexa_float((double)ts.tv_sec + (double)ts.tv_nsec/1e9);
}

HexaVal hexa_random(void) {
    return hexa_float(rand() / (double)RAND_MAX);
}

HexaVal char_code(HexaVal s, HexaVal idx) {
    if (s.tag != TAG_STR) return hexa_int(0);
    int i = idx.i;
    if (i < 0 || i >= (int)strlen(s.s)) return hexa_int(0);
    return hexa_int((unsigned char)s.s[i]);
}

// ── Added: method-dispatch helpers (bt 34) ────────────────────
HexaVal hexa_str_parse_int(HexaVal s) {
    if (s.tag != TAG_STR) return hexa_int(0);
    return hexa_int((int64_t)strtoll(s.s, NULL, 10));
}

HexaVal hexa_str_parse_float(HexaVal s) {
    if (s.tag != TAG_STR) return hexa_float(0.0);
    return hexa_float(strtod(s.s, NULL));
}

HexaVal hexa_str_trim_start(HexaVal s) {
    if (s.tag != TAG_STR) return s;
    char* p = s.s;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return hexa_str_own(strdup(p));
}

HexaVal hexa_str_trim_end(HexaVal s) {
    if (s.tag != TAG_STR) return s;
    int len = strlen(s.s);
    while (len > 0 && (s.s[len-1] == ' ' || s.s[len-1] == '\t' || s.s[len-1] == '\n' || s.s[len-1] == '\r')) len--;
    return hexa_str_own(strndup(s.s, len));
}

// Byte-based slice: [start, end) clamped to length
HexaVal hexa_str_slice(HexaVal s, HexaVal start, HexaVal end) {
    if (s.tag != TAG_STR) return hexa_str("");
    int len = (int)strlen(s.s);
    int a = (int)start.i, b = (int)end.i;
    if (a < 0) a = 0;
    if (b > len) b = len;
    if (a > b) a = b;
    return hexa_str_own(strndup(s.s + a, b - a));
}

HexaVal hexa_array_slice(HexaVal arr, HexaVal start, HexaVal end) {
    if (arr.tag != TAG_ARRAY) return hexa_array_new();
    int n = arr.arr.len;
    int a = (int)start.i, b = (int)end.i;
    if (a < 0) a = 0;
    if (b > n) b = n;
    if (a > b) a = b;
    HexaVal out = hexa_array_new();
    for (int i = a; i < b; i++) out = hexa_array_push(out, arr.arr.items[i]);
    return out;
}

HexaVal hexa_array_map(HexaVal arr, HexaVal fn) {
    if (arr.tag != TAG_ARRAY) return hexa_array_new();
    HexaVal out = hexa_array_new();
    for (int i = 0; i < arr.arr.len; i++) {
        out = hexa_array_push(out, hexa_call1(fn, arr.arr.items[i]));
    }
    return out;
}

HexaVal hexa_array_filter(HexaVal arr, HexaVal fn) {
    if (arr.tag != TAG_ARRAY) return hexa_array_new();
    HexaVal out = hexa_array_new();
    for (int i = 0; i < arr.arr.len; i++) {
        HexaVal keep = hexa_call1(fn, arr.arr.items[i]);
        if (hexa_truthy(keep)) out = hexa_array_push(out, arr.arr.items[i]);
    }
    return out;
}

HexaVal hexa_array_fold(HexaVal arr, HexaVal init, HexaVal fn) {
    if (arr.tag != TAG_ARRAY) return init;
    HexaVal acc = init;
    for (int i = 0; i < arr.arr.len; i++) {
        acc = hexa_call2(fn, acc, arr.arr.items[i]);
    }
    return acc;
}

HexaVal hexa_array_index_of(HexaVal arr, HexaVal item) {
    if (arr.tag != TAG_ARRAY) return hexa_int(-1);
    for (int i = 0; i < arr.arr.len; i++) {
        if (hexa_truthy(hexa_eq(arr.arr.items[i], item))) return hexa_int(i);
    }
    return hexa_int(-1);
}

HexaVal hexa_str_bytes(HexaVal s) {
    if (s.tag != TAG_STR) return hexa_array_new();
    HexaVal out = hexa_array_new();
    for (const unsigned char* p = (const unsigned char*)s.s; *p; p++) {
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
    int c = (code.tag == TAG_INT) ? (int)code.i
          : (code.tag == TAG_FLOAT) ? (int)code.f
          : 0;
    fflush(stdout); fflush(stderr);
    exit(c);
    return hexa_void(); // unreachable
}

HexaVal hexa_sleep(HexaVal sec) {
    double s = (sec.tag == TAG_FLOAT) ? sec.f
             : (sec.tag == TAG_INT)   ? (double)sec.i
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
    if (v.tag == TAG_FLOAT) return v.f;
    if (v.tag == TAG_INT)   return (double)v.i;
    if (v.tag == TAG_BOOL)  return v.b ? 1.0 : 0.0;
    return 0.0;
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

HexaVal hexa_input(HexaVal prompt) {
    if (prompt.tag == TAG_STR && prompt.s && prompt.s[0]) {
        fputs(prompt.s, stdout);
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
    if (v.tag == TAG_STR && v.s && strncmp(v.s, "ERR:", 4) == 0) return hexa_bool(1);
    return hexa_bool(0);
}

HexaVal hexa_read_lines(HexaVal path) {
    HexaVal content = hexa_read_file(path);
    if (content.tag != TAG_STR) return hexa_array_new();
    HexaVal out = hexa_array_new();
    const char* p = content.s;
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
    int64_t code = (n.tag == TAG_INT) ? n.i : (int64_t)_hexa_f(n);
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
    if (name.tag != TAG_STR || !name.s) return hexa_str("");
    const char* v = getenv(name.s);
    return hexa_str(v ? v : "");
}

HexaVal hexa_delete_file(HexaVal path) {
    if (path.tag != TAG_STR || !path.s) return hexa_void();
    (void)unlink(path.s);
    return hexa_void();
}

HexaVal hexa_append_file(HexaVal path, HexaVal content) {
    if (path.tag != TAG_STR || !path.s) return hexa_void();
    const char* data = (content.tag == TAG_STR && content.s) ? content.s : "";
    FILE* f = fopen(path.s, "ab");
    if (!f) return hexa_void();
    fwrite(data, 1, strlen(data), f);
    fclose(f);
    return hexa_void();
}

HexaVal hexa_bin(HexaVal n) {
    uint64_t v = (n.tag == TAG_INT) ? (uint64_t)n.i : (uint64_t)_hexa_f(n);
    char buf[65]; int pos = 0;
    if (v == 0) { buf[pos++] = '0'; }
    while (v > 0 && pos < 64) { buf[pos++] = (char)('0' + (v & 1)); v >>= 1; }
    char* out = (char*)malloc(pos + 1);
    for (int i = 0; i < pos; i++) out[i] = buf[pos - 1 - i];
    out[pos] = 0;
    return hexa_str_own(out);
}

HexaVal hexa_hex(HexaVal n) {
    uint64_t v = (n.tag == TAG_INT) ? (uint64_t)n.i : (uint64_t)_hexa_f(n);
    char* out = (char*)malloc(20);
    snprintf(out, 20, "%llx", (unsigned long long)v);
    return hexa_str_own(out);
}

HexaVal hexa_timestamp(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return hexa_int((int64_t)ts.tv_sec);
}

// Base64 (RFC 4648)
static const char _b64_enc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

HexaVal hexa_base64_encode(HexaVal s) {
    if (s.tag != TAG_STR || !s.s) return hexa_str("");
    const unsigned char* in = (const unsigned char*)s.s;
    size_t n = strlen(s.s);
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
    if (s.tag != TAG_STR || !s.s) return hexa_str("");
    static int dec[256];
    static int dec_init = 0;
    if (!dec_init) {
        for (int i = 0; i < 256; i++) dec[i] = -1;
        for (int i = 0; i < 64; i++) dec[(unsigned char)_b64_enc[i]] = i;
        dec_init = 1;
    }
    const unsigned char* in = (const unsigned char*)s.s;
    size_t n = strlen(s.s);
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

HexaVal timestamp     = {.tag=TAG_FN, .fn={.fn_ptr=(void*)_bt73_timestamp_w,     .arity=0}};
HexaVal base64_encode = {.tag=TAG_FN, .fn={.fn_ptr=(void*)_bt73_base64_encode_w, .arity=1}};
HexaVal base64_decode = {.tag=TAG_FN, .fn={.fn_ptr=(void*)_bt73_base64_decode_w, .arity=1}};

