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
HexaVal hexa_array_new(void);
HexaVal hexa_void(void);
int hexa_is_type(HexaVal v, const char* type_name);

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
    TAG_ARRAY, TAG_MAP, TAG_FN, TAG_CHAR, TAG_CLOSURE
} HexaTag;

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
            char** keys;
            struct HexaVal* vals;
            int len;
            int cap;
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

HexaVal hexa_str(const char* s) {
    HexaVal v = {.tag=TAG_STR};
    v.s = strdup(s);
    return v;
}

HexaVal hexa_str_own(char* s) {
    return (HexaVal){.tag=TAG_STR, .s=s};
}

// ── Array operations ─────────────────────────────────────

HexaVal hexa_array_new() {
    HexaVal v = {.tag=TAG_ARRAY};
    v.arr.items = NULL; v.arr.len = 0; v.arr.cap = 0;
    return v;
}

HexaVal hexa_array_push(HexaVal arr, HexaVal item) {
    int new_len = arr.arr.len + 1;
    if (new_len > arr.arr.cap) {
        int new_cap = new_len < 8 ? 8 : new_len * 2;
        HexaVal* new_items = realloc(arr.arr.items, sizeof(HexaVal) * new_cap);
        if (!new_items) { fprintf(stderr, "OOM in array_push\n"); exit(1); }
        arr.arr.items = new_items;
        arr.arr.cap = new_cap;
    }
    arr.arr.items[arr.arr.len] = item;
    arr.arr.len = new_len;
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

int hexa_len(HexaVal v) {
    if (v.tag == TAG_STR) return strlen(v.s);
    if (v.tag == TAG_ARRAY) return v.arr.len;
    if (v.tag == TAG_MAP) return v.map.len;
    return 0;
}

// ── Map operations ───────────────────────────────────────

HexaVal hexa_map_new() {
    HexaVal v = {.tag=TAG_MAP};
    v.map.keys = NULL; v.map.vals = NULL; v.map.len = 0; v.map.cap = 0;
    return v;
}

HexaVal hexa_map_set(HexaVal m, const char* key, HexaVal val) {
    // Check if key exists
    for (int i = 0; i < m.map.len; i++) {
        if (strcmp(m.map.keys[i], key) == 0) {
            m.map.vals[i] = val;
            return m;
        }
    }
    // Add new key
    int new_len = m.map.len + 1;
    if (new_len > m.map.cap) {
        int new_cap = new_len * 2;
        m.map.keys = realloc(m.map.keys, sizeof(char*) * new_cap);
        m.map.vals = realloc(m.map.vals, sizeof(HexaVal) * new_cap);
        m.map.cap = new_cap;
    }
    m.map.keys[m.map.len] = strdup(key);
    m.map.vals[m.map.len] = val;
    m.map.len = new_len;
    return m;
}

HexaVal hexa_map_get(HexaVal m, const char* key) {
    for (int i = 0; i < m.map.len; i++) {
        if (strcmp(m.map.keys[i], key) == 0) return m.map.vals[i];
    }
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
static HexaVal hexa_map_get_ic_slow(HexaVal m, const char* key, HexaIC* ic) {
    if (hexa_ic_stats_on()) { ic->misses++; g_hexa_ic_misses++; }
    if (m.tag == TAG_MAP) {
        for (int i = 0; i < m.map.len; i++) {
            if (strcmp(m.map.keys[i], key) == 0) {
                ic->keys_ptr = (void*)m.map.keys;
                ic->len      = m.map.len;
                ic->idx      = i;
                return m.map.vals[i];
            }
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
#define hexa_map_get_ic(M, KEY, IC) \
    ({ HexaVal __ic_m = (M); HexaIC* __ic = (IC); \
       ((void*)__ic_m.map.keys == __ic->keys_ptr \
        && __ic_m.map.len == __ic->len \
        && __ic->idx < __ic->len) \
           ? (g_hexa_ic_stats_enabled > 0 \
              ? (__ic->hits++, g_hexa_ic_hits++, __ic_m.map.vals[__ic->idx]) \
              : __ic_m.map.vals[__ic->idx]) \
           : hexa_map_get_ic_slow(__ic_m, (KEY), __ic); })


HexaVal hexa_map_keys(HexaVal m) {
    HexaVal arr = hexa_array_new();
    for (int i = 0; i < m.map.len; i++) {
        arr = hexa_array_push(arr, hexa_str(m.map.keys[i]));
    }
    return arr;
}

HexaVal hexa_map_values(HexaVal m) {
    HexaVal arr = hexa_array_new();
    for (int i = 0; i < m.map.len; i++) {
        arr = hexa_array_push(arr, m.map.vals[i]);
    }
    return arr;
}

int hexa_map_contains_key(HexaVal m, const char* key) {
    for (int i = 0; i < m.map.len; i++) {
        if (strcmp(m.map.keys[i], key) == 0) return 1;
    }
    return 0;
}

HexaVal hexa_map_remove(HexaVal m, const char* key) {
    for (int i = 0; i < m.map.len; i++) {
        if (strcmp(m.map.keys[i], key) == 0) {
            free(m.map.keys[i]);
            for (int j = i; j < m.map.len - 1; j++) {
                m.map.keys[j] = m.map.keys[j+1];
                m.map.vals[j] = m.map.vals[j+1];
            }
            m.map.len--;
            return m;
        }
    }
    return m;
}

// Polymorphic index get: array[int] or map[string]
HexaVal hexa_index_get(HexaVal container, HexaVal key) {
    if (container.tag == TAG_MAP && key.tag == TAG_STR) {
        return hexa_map_get(container, key.s);
    }
    return hexa_array_get(container, key.i);
}

// For-in dispatch: array → element, map → key as string.
// Used by `for x in collection { ... }` codegen so maps iterate their keys
// (Python-style). hexa_len already handles both tags, so no separate length.
HexaVal hexa_iter_get(HexaVal v, int64_t idx) {
    if (v.tag == TAG_MAP) {
        if (idx < 0 || idx >= v.map.len) {
            fprintf(stderr, "map iter index %lld out of bounds (len %d)\n", (long long)idx, v.map.len);
            exit(1);
        }
        return hexa_str(v.map.keys[idx]);
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
int hexa_is_type(HexaVal v, const char* type_name) {
    if (v.tag != TAG_MAP) return 0;
    for (int i = 0; i < v.map.len; i++) {
        if (strcmp(v.map.keys[i], "__type__") == 0) {
            HexaVal t = v.map.vals[i];
            return t.tag == TAG_STR && t.s && strcmp(t.s, type_name) == 0;
        }
    }
    return 0;
}

// ── String operations ────────────────────────────────────

HexaVal hexa_str_concat(HexaVal a, HexaVal b) {
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
        case TAG_STR: return hexa_bool(strcmp(a.s, b.s) == 0);
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
